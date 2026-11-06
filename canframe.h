#ifndef CANFRAME_H
#define CANFRAME_H

#include <QMetaType>
#include <QVector>

// 数据链路层 CAN 帧（值类型，可跨线程传递）
struct CANFrame
{
    quint32 id = 0;
    quint8 dlc = 0;
    quint8 data[8] = {0};
    bool extended = false;
    bool remote = false;
};

Q_DECLARE_METATYPE(CANFrame)
Q_DECLARE_METATYPE(QVector<CANFrame>)

#endif // CANFRAME_H
