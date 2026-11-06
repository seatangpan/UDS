#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include "canthread.h"
#include "udsserver.h"

class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class QProgressBar;
class QTableWidget;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 设备控制
    void onOpenDeviceClicked();
    void onCloseDeviceClicked();

    // 服务控制
    void onStartServerClicked();
    void onStopServerClicked();

    // 镜像
    void onBrowseImageClicked();
    void onClearImageClicked();
    void onSaveImageClicked();

    // 日志
    void onClearLogClicked();
    void onExportLogClicked();
    void onAutoScrollToggled(bool checked);
    void onFlushLog();

    // 服务端事件
    void onUdsMessage(bool tx, quint32 canId, QString data, QString note);
    void onSessionChanged(int session);
    void onSecurityChanged(int level);
    void onDownloadProgress(quint32 written, quint32 total);
    void onStatsUpdated(UdsStats stats);
    void onImageUpdated(QString sha256, quint32 writtenBytes);
    void onResetRequested(int resetType);
    void onServerError(QString error);
    void onServerStateChanged(bool running);
    void onCanError(QString error);

private:
    struct LogEntry
    {
        QString time;
        bool tx;
        quint32 canId;
        QString data;
        QString note;
    };

    void setupUi();
    void setupConnections();
    void updateControlsState();
    void updateStatusBar();
    void appendLogRow(const LogEntry &entry);
    static QString sessionText(int session);
    static QString securityText(int level);
    static quint32 parseUint(const QString &text, quint32 defaultValue, bool *ok = nullptr);

    CANThread *m_canThread;
    UdsServer *m_server;

    // CAN 配置
    QComboBox *m_deviceTypeCombo;
    QSpinBox *m_deviceIndexSpin;
    QComboBox *m_channelCombo;
    QComboBox *m_baudCombo;
    QPushButton *m_openButton;
    QPushButton *m_closeButton;
    QLabel *m_deviceStateLabel;

    // UDS 配置
    QLineEdit *m_requestIdEdit;
    QLineEdit *m_responseIdEdit;
    QLineEdit *m_functionalIdEdit;
    QCheckBox *m_extendedCheck;
    QLineEdit *m_flashBaseEdit;
    QLineEdit *m_flashSizeEdit;
    QSpinBox *m_p2Spin;
    QLineEdit *m_imagePathEdit;
    QPushButton *m_browseButton;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QPushButton *m_clearImageButton;
    QPushButton *m_saveImageButton;

    // 状态
    QLabel *m_sessionLabel;
    QLabel *m_securityLabel;
    QLabel *m_downloadLabel;
    QProgressBar *m_progressBar;
    QLabel *m_shaLabel;

    // 统计
    QLabel *m_requestCountLabel;
    QLabel *m_positiveCountLabel;
    QLabel *m_negativeCountLabel;
    QLabel *m_pendingCountLabel;
    QLabel *m_receiveFramesLabel;
    QLabel *m_sendFramesLabel;
    QLabel *m_frameRateLabel;

    QLabel *m_statusLabel;

    // 日志
    QTableWidget *m_logTable;
    QCheckBox *m_autoScrollCheckBox;
    QPushButton *m_clearLogButton;
    QPushButton *m_exportLogButton;
    QTimer *m_logFlushTimer;
    QVector<LogEntry> m_pendingLog;
    bool m_autoScroll;

    bool m_deviceOpened;
    bool m_serverRunning;
};

#endif // MAINWINDOW_H
