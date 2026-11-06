#ifndef CANTHREAD_H
#define CANTHREAD_H

#include <QThread>
#include <QMutex>
#include "ControlCAN.h"
#include "canframe.h"

class CANThread : public QThread
{
    Q_OBJECT
public:
    explicit CANThread(QObject *parent = nullptr);
    ~CANThread();

    void stop();

    // 1.打开设备（选择通道与波特率）
    bool openDevice(UINT deviceType, UINT deviceIndex, UINT channel, UINT baudRate);

    // 2.初始化CAN
    bool initCAN();

    // 3.启动CAN
    bool startCAN();

    // 4.发送一帧
    bool sendFrame(const CANFrame &frame);

    // 5.关闭设备
    void closeDevice();

    // 0.复位设备
    bool resetCAN();

    UINT getDeviceType() const { return m_deviceType; }
    UINT getDeviceIndex() const { return m_deviceIndex; }
    UINT getChannel() const { return m_channel; }
    bool isRunning() const { return m_running; }

signals:
    void framesReceived(QVector<CANFrame> frames);
    void errorOccurred(QString error);

private:
    void run() override;

    UINT m_deviceType;
    UINT m_deviceIndex;
    UINT m_channel;
    UINT m_baudRate;

    volatile bool m_stopped;
    volatile bool m_running;

    QMutex m_txMutex;
};

#endif // CANTHREAD_H
