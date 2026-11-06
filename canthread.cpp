#include "canthread.h"
#include <QDebug>
#include <string.h>

CANThread::CANThread(QObject *parent)
    : QThread(parent), m_deviceType(0), m_deviceIndex(0), m_channel(0), m_baudRate(0),
      m_stopped(false), m_running(false)
{
}

CANThread::~CANThread()
{
    stop();
    wait(1000);
    closeDevice();
}

void CANThread::stop()
{
    m_stopped = true;
}

bool CANThread::openDevice(UINT deviceType, UINT deviceIndex, UINT channel, UINT baudRate)
{
    m_deviceType = deviceType;
    m_deviceIndex = deviceIndex;
    m_channel = channel;
    m_baudRate = baudRate;

    DWORD result = VCI_OpenDevice(m_deviceType, m_deviceIndex, 0);
    if (result != STATUS_OK)
    {
        emit errorOccurred(QString("打开设备失败，错误码: %1").arg(result));
        return false;
    }

    qDebug() << "CAN设备打开成功";
    return true;
}

bool CANThread::initCAN()
{
    VCI_ClearBuffer(m_deviceType, m_deviceIndex, m_channel);

    VCI_INIT_CONFIG vic;
    memset(&vic, 0, sizeof(VCI_INIT_CONFIG));
    vic.AccCode = 0x00000000;
    vic.AccMask = 0xFFFFFFFF;
    vic.Filter = 1;
    vic.Mode = 0; // 正常模式

    // 根据波特率设置定时器
    switch (m_baudRate)
    {
    case 10:
        vic.Timing0 = 0x31;
        vic.Timing1 = 0x1c;
        break;
    case 20:
        vic.Timing0 = 0x18;
        vic.Timing1 = 0x1c;
        break;
    case 50:
        vic.Timing0 = 0x09;
        vic.Timing1 = 0x1c;
        break;
    case 100:
        vic.Timing0 = 0x04;
        vic.Timing1 = 0x1c;
        break;
    case 125:
        vic.Timing0 = 0x03;
        vic.Timing1 = 0x1c;
        break;
    case 250:
        vic.Timing0 = 0x01;
        vic.Timing1 = 0x1c;
        break;
    case 500:
        vic.Timing0 = 0x00;
        vic.Timing1 = 0x1c;
        break;
    case 800:
        vic.Timing0 = 0x00;
        vic.Timing1 = 0x16;
        break;
    case 1000:
        vic.Timing0 = 0x00;
        vic.Timing1 = 0x14;
        break;
    default:
        vic.Timing0 = 0x00;
        vic.Timing1 = 0x1c; // 默认500K
        break;
    }

    DWORD result = VCI_InitCAN(m_deviceType, m_deviceIndex, m_channel, &vic);
    if (result != STATUS_OK)
    {
        emit errorOccurred(QString("初始化CAN%1失败，错误码: %2").arg(m_channel).arg(result));
        return false;
    }

    qDebug() << "CAN初始化成功";
    return true;
}

bool CANThread::startCAN()
{
    if (VCI_StartCAN(m_deviceType, m_deviceIndex, m_channel) != STATUS_OK)
    {
        emit errorOccurred(QString("启动CAN%1失败").arg(m_channel));
        return false;
    }

    m_running = true;
    qDebug() << "CAN启动成功";
    return true;
}

bool CANThread::sendFrame(const CANFrame &frame)
{
    if (!m_running)
    {
        return false;
    }

    VCI_CAN_OBJ vco;
    memset(&vco, 0, sizeof(VCI_CAN_OBJ));

    vco.ID = frame.id;
    vco.RemoteFlag = frame.remote ? 1 : 0;
    vco.ExternFlag = frame.extended ? 1 : 0;
    vco.DataLen = frame.dlc > 8 ? 8 : frame.dlc;

    for (UINT j = 0; j < vco.DataLen; j++)
    {
        vco.Data[j] = frame.data[j];
    }

    QMutexLocker locker(&m_txMutex);
    ULONG result = VCI_Transmit(m_deviceType, m_deviceIndex, m_channel, &vco, 1);
    return (result > 0);
}

void CANThread::closeDevice()
{
    m_running = false;
    VCI_CloseDevice(m_deviceType, m_deviceIndex);
    qDebug() << "CAN设备已关闭";
}

bool CANThread::resetCAN()
{
    m_running = false;

    if (VCI_ResetCAN(m_deviceType, m_deviceIndex, m_channel) != STATUS_OK)
    {
        emit errorOccurred(QString("复位CAN%1失败").arg(m_channel));
        return false;
    }

    qDebug() << "CAN复位成功";
    return true;
}

void CANThread::run()
{
    VCI_CAN_OBJ vco[256];

    while (!m_stopped)
    {
        ULONG count = VCI_Receive(m_deviceType, m_deviceIndex, m_channel, vco, 256, 1);
        if (count > 0 && count != 0xFFFFFFFF)
        {
            QVector<CANFrame> frames;
            frames.reserve(static_cast<int>(count));
            for (ULONG i = 0; i < count; i++)
            {
                CANFrame frame;
                frame.id = vco[i].ID;
                frame.dlc = vco[i].DataLen > 8 ? 8 : vco[i].DataLen;
                memcpy(frame.data, vco[i].Data, frame.dlc);
                frame.extended = (vco[i].ExternFlag != 0);
                frame.remote = (vco[i].RemoteFlag != 0);
                frames.append(frame);
            }
            emit framesReceived(frames);
        }
    }

    m_stopped = false;
}
