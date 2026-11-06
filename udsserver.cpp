#include "udsserver.h"
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <string.h>

#define S3_TIMEOUT_MS      5500
#define RESET_BUSY_MS      1500
#define PENDING_DELAY_MS   20
#define MAX_BLOCK_LENGTH   0x0FFF
#define HASH_CHUNK_SIZE    (64U * 1024U)

static QString payloadToHex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

static quint32 crc32Update(quint32 crc, const QByteArray &data)
{
    for (int i = 0; i < data.size(); i++)
    {
        crc ^= static_cast<quint32>(static_cast<quint8>(data.at(i))) << 24;
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x80000000U) ? ((crc << 1) ^ 0x04C11DB7U) : (crc << 1);
        }
    }
    return crc;
}

UdsServer *UdsServer::s_instance = nullptr;

UdsServer::UdsServer(QObject *parent)
    : QThread(parent), m_canThread(nullptr), m_stopFlag(false), m_running(false),
      m_session(0x01), m_lastRequestMs(0), m_resetUntilMs(-1), m_dtcSettingEnabled(true),
      m_downloadActive(false), m_downloadAddress(0), m_downloadRemaining(0), m_downloadTotal(0),
      m_blockCounter(0), m_hashActive(false), m_hashOffset(0), m_unerasedWarned(false),
      m_lastStatsMs(0), m_rateMs(0), m_rateFrames(0)
{
    s_instance = this;
    memset(&m_tpLink, 0, sizeof(m_tpLink));
    memset(&m_tpInit, 0, sizeof(m_tpInit));
    memset(m_rxBuffer, 0, sizeof(m_rxBuffer));
}

UdsServer::~UdsServer()
{
    stopServer();
    if (m_canThread)
    {
        disconnect(m_canThread, nullptr, this, nullptr);
    }
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

bool UdsServer::setCanThread(CANThread *canThread)
{
    if (isRunning())
    {
        return false;
    }

    if (m_canThread)
    {
        disconnect(m_canThread, nullptr, this, nullptr);
    }

    m_canThread = canThread;
    if (m_canThread)
    {
        // CAN 接收线程只把帧压入队列，由本线程轮询处理
        connect(m_canThread, &CANThread::framesReceived, this, &UdsServer::onFramesReceived, Qt::DirectConnection);
    }
    return true;
}

bool UdsServer::startServer(const Config &config)
{
    if (isRunning())
    {
        return false;
    }
    if (!m_canThread || !m_canThread->isRunning())
    {
        emit serverError("CAN 设备未启动");
        return false;
    }
    if (config.requestId == config.responseId ||
        config.requestId == config.functionalId ||
        config.responseId == config.functionalId)
    {
        emit serverError("请求 ID、响应 ID、功能 ID 不能相同");
        return false;
    }
    if (config.flashSize < 0x1000)
    {
        emit serverError("镜像大小不能小于 4KB");
        return false;
    }
    if (!config.imagePath.isEmpty())
    {
        QFileInfo info(config.imagePath);
        if (!info.absoluteDir().exists())
        {
            emit serverError("镜像文件目录不存在");
            return false;
        }
    }

    m_config = config;
    m_flash.configure(config.flashBase, config.flashSize, config.imagePath);
    if (!m_flash.open())
    {
        emit serverError("打开镜像文件失败: " + config.imagePath);
        return false;
    }

    m_stopFlag = false;
    start();
    return true;
}

void UdsServer::stopServer()
{
    if (!isRunning())
    {
        return;
    }

    m_stopFlag = true;
    quit();
    wait(3000);
}

void UdsServer::run()
{
    m_running = true;
    m_elapsed.start();
    m_lastRequestMs = m_elapsed.elapsed();
    m_lastStatsMs = m_lastRequestMs;
    m_resetUntilMs = -1;
    m_session = 0x01;
    m_security.reset();
    m_downloadActive = false;
    m_blockCounter = 0;
    m_downloadRanges.clear();
    m_hashActive = false;
    m_hashOffset = 0;
    m_unerasedWarned = false;
    m_dtcSettingEnabled = true;
    m_fingerprint = QByteArray(14, static_cast<char>(0xFF));
    m_stats = UdsStats();
    m_rateMs = m_lastRequestMs;
    m_rateFrames = 0;
    m_activePayload.clear();
    m_txQueue.clear();

    {
        QMutexLocker locker(&m_rxMutex);
        m_rxFrameQueue.clear();
    }

    if (!createTpLink())
    {
        emit serverError("ISO-TP 通道创建失败");
        m_flash.close();
        m_running = false;
        emit serverStateChanged(false);
        return;
    }

    emit serverStateChanged(true);
    emit sessionChanged(m_session);
    emit securityChanged(0);

    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &UdsServer::onPollTimeout, Qt::DirectConnection);
    timer.start(1);

    exec();

    timer.stop();
    m_flash.flush();
    m_flash.close();
    m_running = false;
    emit serverStateChanged(false);
}

bool UdsServer::createTpLink()
{
    memset(&m_tpLink, 0, sizeof(m_tpLink));
    memset(&m_tpInit, 0, sizeof(m_tpInit));

    m_tpInit.usr_arg = this;
    m_tpInit.tx_id.id = m_config.responseId;
    m_tpInit.tx_id.isExt = m_config.extendedFrame ? 1 : 0;
    m_tpInit.tx_id.isCANFD = 0;
    m_tpInit.tx_id.isRemote = 0;

    m_tpInit.rx_id.id = m_config.requestId;
    m_tpInit.rx_id.isExt = m_config.extendedFrame ? 1 : 0;
    m_tpInit.rx_id.isCANFD = 0;
    m_tpInit.rx_id.isRemote = 0;

    m_tpInit.funtion_id.id = m_config.functionalId;
    m_tpInit.funtion_id.isExt = m_config.extendedFrame ? 1 : 0;
    m_tpInit.funtion_id.isCANFD = 0;
    m_tpInit.funtion_id.isRemote = 0;

    m_tpInit.L_Data_request = tpLDataRequest;
    m_tpInit.N_USData_indication = tpNUsDataIndication;
    m_tpInit.N_USData_confirm = tpNUsDataConfirm;
    m_tpInit.print_debug = tpPrintDebug;
    m_tpInit.rx_buff = m_rxBuffer;
    m_tpInit.rx_buff_len = sizeof(m_rxBuffer);

    m_tpInit.N_As = 1000;
    m_tpInit.N_Ar = 1000;
    m_tpInit.N_Bs = 1000;
    m_tpInit.N_Br = 0;
    m_tpInit.N_Cs = 0;
    m_tpInit.N_Cr = 1000;
    m_tpInit.N_WFTmax = 3;
    m_tpInit.FC_BS = 0;
    m_tpInit.STmin = 0;
    m_tpInit.TX_DLC = 0x8;
    m_tpInit.frame_pad = 0xFF;

    return iso_can_tp_create(&m_tpLink, &m_tpInit) == OP_OK;
}

int UdsServer::tpLDataRequest(can_iso_tp_link_t_p link, const struct CAN_msg *msg)
{
    if (!s_instance || !s_instance->sendCanFrame(msg))
    {
        return 1;
    }
    iso_can_tp_L_Data_confirm(link, msg, 0);
    return 0;
}

void UdsServer::tpNUsDataIndication(can_iso_tp_link_t_p link, const uint8_t payload[], uint32_t size, CAN_ISO_TP_RESAULT error)
{
    Q_UNUSED(link)

    if (!s_instance)
    {
        return;
    }

    if (error == N_OK)
    {
        s_instance->processRequest(QByteArray(reinterpret_cast<const char *>(payload), static_cast<int>(size)));
    }
    else
    {
        s_instance->handleTpError(error);
    }
}

void UdsServer::tpNUsDataConfirm(can_iso_tp_link_t_p link, const uint8_t payload[], uint32_t size, CAN_ISO_TP_RESAULT error)
{
    Q_UNUSED(link)
    Q_UNUSED(payload)
    Q_UNUSED(size)

    if (s_instance && error != N_OK)
    {
        s_instance->handleTpError(error);
    }
}

void UdsServer::tpPrintDebug(const char *str)
{
    Q_UNUSED(str)
}

bool UdsServer::sendCanFrame(const struct CAN_msg *msg)
{
    if (!m_canThread)
    {
        return false;
    }

    CANFrame frame;
    frame.id = msg->id.id;
    frame.dlc = msg->dlc > 8 ? 8 : msg->dlc;
    memcpy(frame.data, msg->data, frame.dlc);
    frame.extended = (msg->id.isExt != 0);
    frame.remote = (msg->id.isRemote != 0);

    const bool sent = m_canThread->sendFrame(frame);
    if (sent)
    {
        m_stats.framesSent++;
        appendLog(true, frame.id,
                  QByteArray(reinterpret_cast<const char *>(frame.data), frame.dlc), QString());
    }
    return sent;
}

void UdsServer::handleTpError(int error)
{
    emit udsMessage(false, 0, QString(), QString("ISO-TP 错误: %1").arg(error));
}

void UdsServer::onFramesReceived(QVector<CANFrame> frames)
{
    if (!m_running)
    {
        return;
    }

    QMutexLocker locker(&m_rxMutex);
    for (const CANFrame &frame : frames)
    {
        m_rxFrameQueue.enqueue(frame);
    }
}

void UdsServer::onPollTimeout()
{
    if (m_stopFlag || !m_running)
    {
        return;
    }

    const qint64 now = m_elapsed.elapsed();

    QQueue<CANFrame> frames;
    {
        QMutexLocker locker(&m_rxMutex);
        frames.swap(m_rxFrameQueue);
    }

    for (const CANFrame &frame : frames)
    {
        appendLog(false, frame.id,
                  QByteArray(reinterpret_cast<const char *>(frame.data), frame.dlc), QString());

        if (frame.remote)
        {
            continue;
        }
        if (frame.extended != m_config.extendedFrame)
        {
            continue;
        }

        const bool isPhysical = (frame.id == m_config.requestId);
        const bool isFunctional = (frame.id == m_config.functionalId);
        if (!isPhysical && !isFunctional)
        {
            continue;
        }

        m_lastRequestMs = now;
        m_stats.framesReceived++;

        struct CAN_msg msg;
        memset(&msg, 0, sizeof(msg));
        msg.id.id = isFunctional ? m_config.requestId : frame.id;
        msg.id.isExt = frame.extended ? 1 : 0;
        msg.dlc = frame.dlc;
        memcpy(msg.data, frame.data, frame.dlc);

        iso_can_tp_L_Data_indication(&m_tpLink, &msg);
    }

    iso_can_tp_poll(&m_tpLink, static_cast<unsigned int>(now));

    if (m_tpLink.tx_record.status == tx_idle)
    {
        if (!m_activePayload.isEmpty())
        {
            m_activePayload.clear();
        }

        if (!m_txQueue.isEmpty() && m_txQueue.head().dueMs <= now)
        {
            TxItem item = m_txQueue.dequeue();
            if (iso_can_tp_N_USData_request(&m_tpLink, 0,
                                            reinterpret_cast<const uint8_t *>(item.payload.constData()),
                                            static_cast<uint32_t>(item.payload.size())) == OP_OK)
            {
                m_activePayload = item.payload;
            }
            else
            {
                m_txQueue.prepend(item);
            }
        }
    }

    if (m_hashActive && m_txQueue.isEmpty() && m_tpLink.tx_record.status == tx_idle)
    {
        stepImageHash();
    }

    if (m_resetUntilMs >= 0 && now >= m_resetUntilMs)
    {
        m_resetUntilMs = -1;
    }

    if (m_session != 0x01 && m_resetUntilMs < 0 && (now - m_lastRequestMs) > S3_TIMEOUT_MS)
    {
        appendLog(false, 0, QByteArray(), "S3 超时，回到默认会话");
        resetDiagnosticState();
    }

    if (now - m_lastStatsMs >= 200)
    {
        m_lastStatsMs = now;

        if (now - m_rateMs >= 1000)
        {
            const quint64 frames = m_stats.framesReceived + m_stats.framesSent;
            m_stats.frameRate = static_cast<double>(frames - m_rateFrames) * 1000.0
                                / static_cast<double>(now - m_rateMs);
            m_rateMs = now;
            m_rateFrames = frames;
        }

        emit statsUpdated(m_stats);
    }
}

void UdsServer::processRequest(const QByteArray &request)
{
    if (request.isEmpty())
    {
        return;
    }

    const qint64 now = m_elapsed.elapsed();
    m_stats.requestsReceived++;

    const quint8 sid = static_cast<quint8>(request.at(0));

    if (m_resetUntilMs >= 0 && now < m_resetUntilMs)
    {
        respondNegative(sid, 0x21);
        return;
    }

    switch (sid)
    {
    case 0x10:
        handleSessionControl(request);
        break;
    case 0x11:
        handleEcuReset(request);
        break;
    case 0x14:
        handleClearDiagnosticInformation(request);
        break;
    case 0x22:
        handleReadDataByIdentifier(request);
        break;
    case 0x27:
        handleSecurityAccess(request);
        break;
    case 0x28:
        handleCommunicationControl(request);
        break;
    case 0x2E:
        handleWriteDataByIdentifier(request);
        break;
    case 0x31:
        handleRoutineControl(request);
        break;
    case 0x34:
        handleRequestDownload(request);
        break;
    case 0x36:
        handleTransferData(request);
        break;
    case 0x37:
        handleTransferExit(request);
        break;
    case 0x3E:
        handleTesterPresent(request);
        break;
    case 0x85:
        handleControlDtcSetting(request);
        break;
    default:
        respondNegative(sid, 0x11);
        break;
    }
}

void UdsServer::handleSessionControl(const QByteArray &request)
{
    if (request.size() < 2)
    {
        respondNegative(0x10, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));
    const quint8 session = sub & 0x7F;
    const bool suppress = (sub & 0x80) != 0;

    if (session != 0x01 && session != 0x02 && session != 0x03)
    {
        respondNegative(0x10, 0x12);
        return;
    }

    if (session != m_session)
    {
        m_security.reset();
        m_downloadActive = false;
        m_session = session;
    }
    emit sessionChanged(m_session);
    emit securityChanged(m_security.getUnlockedLevel());

    if (!suppress)
    {
        const quint16 p2 = m_config.p2Ms;
        const quint16 p2Star = (0x02 == session) ? 0x01F4U : 0x07D0U;

        QByteArray response;
        response.append(static_cast<char>(0x50));
        response.append(static_cast<char>(session));
        response.append(static_cast<char>((p2 >> 8) & 0xFF));
        response.append(static_cast<char>(p2 & 0xFF));
        response.append(static_cast<char>((p2Star >> 8) & 0xFF));
        response.append(static_cast<char>(p2Star & 0xFF));
        respondPositive(response);
    }
}

void UdsServer::handleEcuReset(const QByteArray &request)
{
    if (request.size() < 2)
    {
        respondNegative(0x11, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));
    const quint8 resetType = sub & 0x7F;
    const bool suppress = (sub & 0x80) != 0;

    if (resetType != 0x01 && resetType != 0x02 && resetType != 0x03)
    {
        respondNegative(0x11, 0x12);
        return;
    }

    if (!suppress)
    {
        QByteArray response;
        response.append(static_cast<char>(0x51));
        response.append(static_cast<char>(resetType));
        respondPositive(response);
    }

    resetDiagnosticState();
    m_resetUntilMs = m_elapsed.elapsed() + RESET_BUSY_MS;
    emit resetRequested(resetType);
}

void UdsServer::handleClearDiagnosticInformation(const QByteArray &request)
{
    if (request.size() != 4)
    {
        respondNegative(0x14, 0x13);
        return;
    }

    respondPositive(QByteArray(1, static_cast<char>(0x54)));
}

void UdsServer::handleReadDataByIdentifier(const QByteArray &request)
{
    if (request.size() < 3 || ((request.size() - 1) % 2) != 0)
    {
        respondNegative(0x22, 0x13);
        return;
    }

    QByteArray response;
    response.append(static_cast<char>(0x62));

    for (int i = 1; i + 1 < request.size(); i += 2)
    {
        quint16 did = (static_cast<quint16>(static_cast<quint8>(request.at(i))) << 8)
                      | static_cast<quint8>(request.at(i + 1));
        QByteArray value;
        if (!readDid(did, value))
        {
            respondNegative(0x22, 0x31);
            return;
        }
        response.append(static_cast<char>((did >> 8) & 0xFF));
        response.append(static_cast<char>(did & 0xFF));
        response.append(value);
    }

    respondPositive(response);
}

void UdsServer::handleSecurityAccess(const QByteArray &request)
{
    if (request.size() < 2)
    {
        respondNegative(0x27, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));

    if (m_session == 0x01)
    {
        respondNegative(0x27, 0x7F);
        return;
    }
    if (sub != 0x01 && sub != 0x02 && sub != 0x03 && sub != 0x04)
    {
        respondNegative(0x27, 0x12);
        return;
    }
    if (m_security.isAttemptsExceeded())
    {
        respondNegative(0x27, 0x36);
        return;
    }

    if (sub & 0x01)
    {
        if (request.size() != 2)
        {
            respondNegative(0x27, 0x13);
            return;
        }

        QByteArray seed = m_security.generateSeed(sub);
        if (seed.isEmpty())
        {
            respondNegative(0x27, 0x12);
            return;
        }

        QByteArray response;
        response.append(static_cast<char>(0x67));
        response.append(static_cast<char>(sub));
        response.append(seed);
        respondPositive(response);
    }
    else
    {
        const quint8 level = sub - 1;
        if (!m_security.hasSeed(level))
        {
            respondNegative(0x27, 0x24);
            return;
        }

        QByteArray key = request.mid(2);
        if (m_security.verifyKey(level, key))
        {
            QByteArray response;
            response.append(static_cast<char>(0x67));
            response.append(static_cast<char>(sub));
            respondPositive(response);
            emit securityChanged(m_security.getUnlockedLevel());
        }
        else
        {
            respondNegative(0x27, m_security.isAttemptsExceeded() ? 0x36 : 0x35);
        }
    }
}

void UdsServer::handleCommunicationControl(const QByteArray &request)
{
    if (request.size() != 3)
    {
        respondNegative(0x28, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));
    const quint8 mode = sub & 0x7F;
    const bool suppress = (sub & 0x80) != 0;

    if (m_session == 0x01 && mode != 0x00)
    {
        respondNegative(0x28, 0x7F);
        return;
    }
    if (mode > 0x03)
    {
        respondNegative(0x28, 0x12);
        return;
    }

    if (!suppress)
    {
        QByteArray response;
        response.append(static_cast<char>(0x68));
        response.append(static_cast<char>(mode));
        respondPositive(response);
    }
}

void UdsServer::handleWriteDataByIdentifier(const QByteArray &request)
{
    if (request.size() < 4)
    {
        respondNegative(0x2E, 0x13);
        return;
    }
    if (!requireProgrammingSecurity(0x2E))
    {
        return;
    }

    quint16 did = (static_cast<quint16>(static_cast<quint8>(request.at(1))) << 8)
                  | static_cast<quint8>(request.at(2));
    if (did != 0xF184)
    {
        respondNegative(0x2E, 0x31);
        return;
    }

    respondPending(0x2E);
    m_fingerprint = request.mid(3);

    QByteArray response;
    response.append(static_cast<char>(0x6E));
    response.append(static_cast<char>(0xF1));
    response.append(static_cast<char>(0x84));
    respondPositive(response, PENDING_DELAY_MS);
}

void UdsServer::handleRoutineControl(const QByteArray &request)
{
    if (request.size() < 4)
    {
        respondNegative(0x31, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));
    const quint8 controlType = sub & 0x7F;
    const bool suppress = (sub & 0x80) != 0;
    const quint16 routineId = (static_cast<quint16>(static_cast<quint8>(request.at(2))) << 8)
                              | static_cast<quint8>(request.at(3));

    if (controlType != 0x01)
    {
        respondNegative(0x31, 0x12);
        return;
    }

    QByteArray response;
    response.append(static_cast<char>(0x71));
    response.append(static_cast<char>(0x01));
    response.append(static_cast<char>((routineId >> 8) & 0xFF));
    response.append(static_cast<char>(routineId & 0xFF));

    switch (routineId)
    {
    case 0x0203:
        if (m_session == 0x01)
        {
            respondNegative(0x31, 0x7F);
            return;
        }
        if (request.size() != 4)
        {
            respondNegative(0x31, 0x13);
            return;
        }
        if (!suppress)
        {
            respondPositive(response);
        }
        break;

    case 0x0202:
    {
        if (!requireProgrammingSecurity(0x31))
        {
            return;
        }
        if (request.size() != 8)
        {
            respondNegative(0x31, 0x13);
            return;
        }

        quint32 reference = 0;
        for (int i = 0; i < 4; i++)
        {
            reference = (reference << 8) | static_cast<quint8>(request.at(4 + i));
        }

        const quint32 actual = downloadCrc();
        if (reference != actual)
        {
            appendLog(false, 0, QByteArray(),
                      QString("CRC 校验失败: 引用 %1 实际 %2")
                          .arg(reference, 8, 16, QLatin1Char('0'))
                          .arg(actual, 8, 16, QLatin1Char('0')));
            respondNegative(0x31, 0x24);
            return;
        }

        if (!suppress)
        {
            respondPositive(response);
        }
        break;
    }

    case 0xFF00:
    {
        if (!requireProgrammingSecurity(0x31))
        {
            return;
        }

        QByteArray record = request.mid(4);
        if (record.size() >= 9 && static_cast<quint8>(record.at(0)) == 0x44)
        {
            quint32 address = 0;
            quint32 length = 0;
            for (int i = 0; i < 4; i++)
            {
                address = (address << 8) | static_cast<quint8>(record.at(1 + i));
                length = (length << 8) | static_cast<quint8>(record.at(5 + i));
            }
            if (!m_flash.contains(address, length))
            {
                respondNegative(0x31, 0x31);
                return;
            }
            if (!suppress)
            {
                respondPending(0x31);
            }
            m_flash.eraseRange(address, length);
            m_flash.flush();
            dropRange(address, length);
            m_hashActive = false;
            if (!suppress)
            {
                response.append(static_cast<char>(0x00));
                respondPositive(response, PENDING_DELAY_MS);
            }
        }
        else
        {
            if (!suppress)
            {
                response.append(static_cast<char>(0x00));
                respondPositive(response);
            }
        }
        break;
    }

    case 0xFF01:
        if (!requireProgrammingSecurity(0x31))
        {
            return;
        }
        if (!suppress)
        {
            response.append(static_cast<char>(0x00));
            respondPending(0x31);
            respondPositive(response, PENDING_DELAY_MS);
        }
        break;

    default:
        respondNegative(0x31, 0x31);
        break;
    }
}

void UdsServer::handleRequestDownload(const QByteArray &request)
{
    if (request.size() < 4)
    {
        respondNegative(0x34, 0x13);
        return;
    }
    if (!requireProgrammingSecurity(0x34))
    {
        return;
    }

    const quint8 dataFormat = static_cast<quint8>(request.at(1));
    const quint8 addressLengthFormat = static_cast<quint8>(request.at(2));
    const quint8 addressLength = addressLengthFormat & 0x0F;
    const quint8 sizeLength = (addressLengthFormat >> 4) & 0x0F;

    if (dataFormat != 0x00)
    {
        respondNegative(0x34, 0x31);
        return;
    }
    if (addressLength < 1 || addressLength > 4 || sizeLength < 1 || sizeLength > 4 ||
        request.size() != 3 + addressLength + sizeLength)
    {
        respondNegative(0x34, 0x13);
        return;
    }

    quint32 address = 0;
    quint32 length = 0;
    for (int i = 0; i < addressLength; i++)
    {
        address = (address << 8) | static_cast<quint8>(request.at(3 + i));
    }
    for (int i = 0; i < sizeLength; i++)
    {
        length = (length << 8) | static_cast<quint8>(request.at(3 + addressLength + i));
    }

    if (length == 0 || !m_flash.contains(address, length))
    {
        respondNegative(0x34, 0x31);
        return;
    }

    m_downloadAddress = address;
    m_downloadRemaining = length;
    m_downloadTotal = length;
    m_blockCounter = 0;
    m_downloadActive = true;
    m_unerasedWarned = false;

    QByteArray response;
    response.append(static_cast<char>(0x74));
    response.append(static_cast<char>(0x20));       // 最大块长度 2 字节
    response.append(static_cast<char>((MAX_BLOCK_LENGTH >> 8) & 0xFF));
    response.append(static_cast<char>(MAX_BLOCK_LENGTH & 0xFF));
    respondPositive(response);

    emit downloadProgress(0, m_downloadTotal);
}

void UdsServer::handleTransferData(const QByteArray &request)
{
    if (!m_downloadActive)
    {
        respondNegative(0x36, 0x24);
        return;
    }
    if (!requireProgrammingSecurity(0x36))
    {
        return;
    }
    if (request.size() < 3)
    {
        respondNegative(0x36, 0x13);
        return;
    }

    const quint8 sequence = static_cast<quint8>(request.at(1));
    const quint8 expected = static_cast<quint8>(m_blockCounter + 1);
    if (sequence != expected)
    {
        respondNegative(0x36, 0x24);
        return;
    }

    QByteArray data = request.mid(2);
    if (static_cast<quint32>(data.size()) > m_downloadRemaining ||
        !m_flash.write(m_downloadAddress, data))
    {
        respondNegative(0x36, 0x31);
        return;
    }

    m_hashActive = false;
    recordRange(m_downloadAddress, static_cast<quint32>(data.size()));
    if (!m_unerasedWarned &&
        !m_flash.isErased(m_downloadAddress, static_cast<quint32>(data.size())))
    {
        m_unerasedWarned = true;
        appendLog(false, 0, QByteArray(),
                  QString("写入未擦除区域: 0x%1 长度 %2")
                      .arg(m_downloadAddress, 8, 16, QLatin1Char('0'))
                      .arg(data.size()));
    }

    m_downloadAddress += static_cast<quint32>(data.size());
    m_downloadRemaining -= static_cast<quint32>(data.size());
    m_blockCounter = sequence;

    QByteArray response;
    response.append(static_cast<char>(0x76));
    response.append(static_cast<char>(sequence));
    respondPositive(response);

    emit downloadProgress(m_downloadTotal - m_downloadRemaining, m_downloadTotal);
}

void UdsServer::handleTransferExit(const QByteArray &request)
{
    Q_UNUSED(request)

    if (!m_downloadActive)
    {
        respondNegative(0x37, 0x24);
        return;
    }
    if (!requireProgrammingSecurity(0x37))
    {
        return;
    }
    if (m_downloadRemaining != 0)
    {
        appendLog(false, 0, QByteArray(), QString("传输未完成，剩余 %1 字节").arg(m_downloadRemaining));
        respondNegative(0x37, 0x24);
        return;
    }

    m_downloadActive = false;
    m_flash.flush();

    respondPositive(QByteArray(1, static_cast<char>(0x77)));

    emit downloadProgress(m_downloadTotal, m_downloadTotal);

    m_imageHash.reset();
    m_hashOffset = 0;
    m_hashActive = true;
}

void UdsServer::handleTesterPresent(const QByteArray &request)
{
    if (request.size() < 2)
    {
        respondNegative(0x3E, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));
    if (!(sub & 0x80))
    {
        QByteArray response;
        response.append(static_cast<char>(0x7E));
        response.append(static_cast<char>(0x00));
        respondPositive(response);
    }
}

void UdsServer::handleControlDtcSetting(const QByteArray &request)
{
    if (request.size() < 2)
    {
        respondNegative(0x85, 0x13);
        return;
    }

    const quint8 sub = static_cast<quint8>(request.at(1));
    const quint8 mode = sub & 0x7F;
    const bool suppress = (sub & 0x80) != 0;

    if (mode > 0x02)
    {
        respondNegative(0x85, 0x12);
        return;
    }

    m_dtcSettingEnabled = (mode == 0x01);

    if (!suppress)
    {
        QByteArray response;
        response.append(static_cast<char>(0xC5));
        response.append(static_cast<char>(mode));
        respondPositive(response);
    }
}

bool UdsServer::requireProgramming(quint8 sid)
{
    if (m_session != 0x02)
    {
        respondNegative(sid, 0x7F);
        return false;
    }
    return true;
}

bool UdsServer::requireProgrammingSecurity(quint8 sid)
{
    if (!requireProgramming(sid))
    {
        return false;
    }
    if (m_security.getUnlockedLevel() == 0)
    {
        respondNegative(sid, 0x33);
        return false;
    }
    return true;
}

bool UdsServer::readDid(quint16 did, QByteArray &value) const
{
    switch (did)
    {
    case 0xF184:
        value = m_fingerprint;
        return true;
    case 0xF187:
        value = QByteArray("1003201904291", 13);
        return true;
    case 0xF189:
        value = QByteArray("LSEA1234567890123", 17);
        return true;
    case 0xF191:
        value = QByteArray("UDS-SIM-01", 10);
        return true;
    case 0xF193:
    case 0xF195:
        value = QByteArray(14, static_cast<char>(0xFF));
        return true;
    default:
        return false;
    }
}

void UdsServer::recordRange(quint32 address, quint32 length)
{
    if (length == 0)
    {
        return;
    }

    const quint64 end = static_cast<quint64>(address) + length;
    for (int i = 0; i < m_downloadRanges.size(); i++)
    {
        const quint64 rangeStart = m_downloadRanges.at(i).address;
        const quint64 rangeEnd = rangeStart + m_downloadRanges.at(i).length;
        if (address >= rangeStart && end <= rangeEnd)
        {
            return;
        }
        if (rangeEnd == address)
        {
            m_downloadRanges[i].length += length;
            return;
        }
        if (end == rangeStart)
        {
            m_downloadRanges[i].address = address;
            m_downloadRanges[i].length += length;
            return;
        }
    }

    DownloadRange range;
    range.address = address;
    range.length = length;
    m_downloadRanges.append(range);
}

void UdsServer::dropRange(quint32 address, quint32 length)
{
    if (length == 0)
    {
        return;
    }

    const quint64 eraseStart = address;
    const quint64 eraseEnd = static_cast<quint64>(address) + length;
    QVector<DownloadRange> kept;

    for (int i = 0; i < m_downloadRanges.size(); i++)
    {
        const DownloadRange &range = m_downloadRanges.at(i);
        const quint64 rangeStart = range.address;
        const quint64 rangeEnd = rangeStart + range.length;
        if (rangeEnd <= eraseStart || rangeStart >= eraseEnd)
        {
            kept.append(range);
            continue;
        }
        if (rangeStart < eraseStart)
        {
            DownloadRange head;
            head.address = range.address;
            head.length = static_cast<quint32>(eraseStart - rangeStart);
            kept.append(head);
        }
        if (eraseEnd < rangeEnd)
        {
            DownloadRange tail;
            tail.address = address + length;
            tail.length = static_cast<quint32>(rangeEnd - eraseEnd);
            kept.append(tail);
        }
    }

    m_downloadRanges = kept;
}

void UdsServer::stepImageHash()
{
    const quint32 size = m_flash.getSize();
    quint32 length = (m_hashOffset < size) ? (size - m_hashOffset) : 0U;

    if (length == 0U)
    {
        m_hashActive = false;
        emit imageUpdated(QString::fromLatin1(m_imageHash.result().toHex()),
                          m_flash.getWrittenBytes());
        return;
    }

    if (length > HASH_CHUNK_SIZE)
    {
        length = HASH_CHUNK_SIZE;
    }

    const QByteArray chunk = m_flash.read(m_flash.getBase() + m_hashOffset, length);
    if (chunk.isEmpty())
    {
        m_hashActive = false;
        return;
    }

    m_imageHash.addData(chunk);
    m_hashOffset += static_cast<quint32>(chunk.size());
}

quint32 UdsServer::downloadCrc() const
{
    quint32 crc = 0xFFFFFFFFU;

    for (int i = 0; i < m_downloadRanges.size(); i++)
    {
        const DownloadRange &range = m_downloadRanges.at(i);
        crc = crc32Update(crc, m_flash.read(range.address, range.length));
    }

    return crc ^ 0xFFFFFFFFU;
}

void UdsServer::resetDiagnosticState()
{
    m_session = 0x01;
    m_security.reset();
    m_downloadActive = false;
    m_blockCounter = 0;
    m_downloadRanges.clear();
    emit sessionChanged(m_session);
    emit securityChanged(0);
}

void UdsServer::enqueueResponse(const QByteArray &payload, int delayMs)
{
    TxItem item;
    item.payload = payload;
    item.dueMs = m_elapsed.elapsed() + delayMs;
    m_txQueue.enqueue(item);
}

void UdsServer::respondPositive(const QByteArray &payload, int delayMs)
{
    enqueueResponse(payload, delayMs);
    m_stats.positiveResponses++;
}

void UdsServer::respondNegative(quint8 sid, quint8 nrc)
{
    QByteArray response;
    response.append(static_cast<char>(0x7F));
    response.append(static_cast<char>(sid));
    response.append(static_cast<char>(nrc));
    enqueueResponse(response);
    m_stats.negativeResponses++;
}

void UdsServer::respondPending(quint8 sid)
{
    QByteArray response;
    response.append(static_cast<char>(0x7F));
    response.append(static_cast<char>(sid));
    response.append(static_cast<char>(0x78));
    enqueueResponse(response);
    m_stats.pendingResponses++;
}

void UdsServer::appendLog(bool tx, quint32 canId, const QByteArray &payload, const QString &note)
{
    emit udsMessage(tx, canId, payload.isEmpty() ? QString() : payloadToHex(payload), note);
}
