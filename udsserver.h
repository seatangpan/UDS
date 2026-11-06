#ifndef UDSSERVER_H
#define UDSSERVER_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QVector>
#include <QElapsedTimer>
#include <QMetaType>
#include <QCryptographicHash>
#include "canthread.h"
#include "canframe.h"
#include "flashimage.h"
#include "securityaccess.h"
#include "can_iso_tp_private.h"

// UDS 通信统计
struct UdsStats
{
    quint64 requestsReceived = 0;  // 收到的请求数
    quint64 positiveResponses = 0; // 肯定响应数
    quint64 negativeResponses = 0; // 否定响应数
    quint64 pendingResponses = 0;  // 0x78 挂起响应数
    quint64 framesReceived = 0;    // 收到的 CAN 帧数
    quint64 framesSent = 0;        // 发送的 CAN 帧数
    double frameRate = 0.0;        // 帧率（帧/秒，1s 窗口）
};

Q_DECLARE_METATYPE(UdsStats)

// OTA 调试用 UDS 服务端（ISO 15765-2 + ISO 14229）
class UdsServer : public QThread
{
    Q_OBJECT
public:
    struct Config
    {
        quint32 requestId = 0x7E0;
        quint32 responseId = 0x7E8;
        quint32 functionalId = 0x7DF;
        bool extendedFrame = false;
        quint32 flashBase = 0x80040000;
        quint32 flashSize = 0x00400000;
        quint16 p2Ms = 50;
        QString imagePath;
    };

    explicit UdsServer(QObject *parent = nullptr);
    ~UdsServer();

    bool setCanThread(CANThread *canThread);
    bool startServer(const Config &config);
    void stopServer();

signals:
    void udsMessage(bool tx, quint32 canId, QString data, QString note);
    void sessionChanged(int session);
    void securityChanged(int level);
    void downloadProgress(quint32 written, quint32 total);
    void statsUpdated(UdsStats stats);
    void imageUpdated(QString sha256, quint32 writtenBytes);
    void resetRequested(int resetType);
    void serverError(QString error);
    void serverStateChanged(bool running);

private slots:
    void onFramesReceived(QVector<CANFrame> frames);
    void onPollTimeout();

private:
    void run() override;

    // ISO-TP 回调
    static int tpLDataRequest(can_iso_tp_link_t_p link, const struct CAN_msg *msg);
    static void tpNUsDataIndication(can_iso_tp_link_t_p link, const uint8_t payload[], uint32_t size, CAN_ISO_TP_RESAULT error);
    static void tpNUsDataConfirm(can_iso_tp_link_t_p link, const uint8_t payload[], uint32_t size, CAN_ISO_TP_RESAULT error);
    static void tpPrintDebug(const char *str);

    bool createTpLink();
    bool sendCanFrame(const struct CAN_msg *msg);
    void processRequest(const QByteArray &request);
    void handleTpError(int error);

    // 服务处理
    void handleSessionControl(const QByteArray &request);
    void handleEcuReset(const QByteArray &request);
    void handleClearDiagnosticInformation(const QByteArray &request);
    void handleReadDataByIdentifier(const QByteArray &request);
    void handleSecurityAccess(const QByteArray &request);
    void handleCommunicationControl(const QByteArray &request);
    void handleWriteDataByIdentifier(const QByteArray &request);
    void handleRoutineControl(const QByteArray &request);
    void handleRequestDownload(const QByteArray &request);
    void handleTransferData(const QByteArray &request);
    void handleTransferExit(const QByteArray &request);
    void handleTesterPresent(const QByteArray &request);
    void handleControlDtcSetting(const QByteArray &request);

    bool requireProgramming(quint8 sid);
    bool requireProgrammingSecurity(quint8 sid);
    bool readDid(quint16 did, QByteArray &value) const;
    quint32 downloadCrc() const;
    void recordRange(quint32 address, quint32 length);
    void dropRange(quint32 address, quint32 length);
    void stepImageHash();
    void resetDiagnosticState();

    // 响应
    void enqueueResponse(const QByteArray &payload, int delayMs = 0);
    void respondPositive(const QByteArray &payload, int delayMs = 0);
    void respondNegative(quint8 sid, quint8 nrc);
    void respondPending(quint8 sid);
    void appendLog(bool tx, quint32 canId, const QByteArray &payload, const QString &note);

    static UdsServer *s_instance;

    CANThread *m_canThread;
    Config m_config;
    bool m_stopFlag;
    bool m_running;

    can_iso_tp_link_t m_tpLink;
    can_iso_tp_init_t m_tpInit;
    uint8_t m_rxBuffer[8192];
    QElapsedTimer m_elapsed;

    QMutex m_rxMutex;
    QQueue<CANFrame> m_rxFrameQueue;

    struct TxItem
    {
        QByteArray payload;
        qint64 dueMs;
    };
    QQueue<TxItem> m_txQueue;
    QByteArray m_activePayload;

    FlashImage m_flash;
    SecurityAccess m_security;

    quint8 m_session;
    qint64 m_lastRequestMs;
    qint64 m_resetUntilMs;
    bool m_dtcSettingEnabled;
    bool m_downloadActive;
    quint32 m_downloadAddress;
    quint32 m_downloadRemaining;
    quint32 m_downloadTotal;
    quint8 m_blockCounter;
    QByteArray m_fingerprint;

    struct DownloadRange
    {
        quint32 address;
        quint32 length;
    };
    QVector<DownloadRange> m_downloadRanges;

    QCryptographicHash m_imageHash{QCryptographicHash::Sha256};
    bool m_hashActive;
    quint32 m_hashOffset;
    bool m_unerasedWarned;

    UdsStats m_stats;
    qint64 m_lastStatsMs;
    qint64 m_rateMs;
    quint64 m_rateFrames;
};

#endif // UDSSERVER_H
