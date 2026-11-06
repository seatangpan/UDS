#include <QApplication>
#include <QMetaType>
#include <windows.h>
#include "mainwindow.h"
#include "canframe.h"
#include "udsserver.h"

int main(int argc, char *argv[])
{
    // Windows 默认定时器精度约 15ms，ISO-TP 轮询需要 1ms
    timeBeginPeriod(1);

    QApplication app(argc, argv);

    qRegisterMetaType<CANFrame>("CANFrame");
    qRegisterMetaType<QVector<CANFrame>>("QVector<CANFrame>");
    qRegisterMetaType<UdsStats>("UdsStats");

    MainWindow window;
    window.show();

    const int ret = app.exec();

    timeEndPeriod(1);
    return ret;
}
