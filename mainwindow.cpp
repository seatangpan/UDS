#include "mainwindow.h"

#include <QWidget>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QTime>
#include <QFile>
#include <QTextStream>
#include <QFont>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_canThread(nullptr), m_server(nullptr),
      m_logFlushTimer(nullptr), m_autoScroll(true),
      m_deviceOpened(false), m_serverRunning(false)
{
    setupUi();
    setupConnections();

    m_logFlushTimer = new QTimer(this);
    m_logFlushTimer->setInterval(100);
    connect(m_logFlushTimer, &QTimer::timeout, this, &MainWindow::onFlushLog);
    m_logFlushTimer->start();

    updateControlsState();
    updateStatusBar();
}

MainWindow::~MainWindow()
{
    if (m_server)
    {
        m_server->stopServer();
    }

    if (m_canThread)
    {
        m_canThread->stop();
        m_canThread->wait(1000);
        delete m_canThread;
        m_canThread = nullptr;
    }
}

void MainWindow::setupUi()
{
    setWindowTitle("UDS 服务端 - OTA 调试模拟器");
    resize(1080, 760);

    QWidget *central = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);

    // CAN 设备配置
    QGroupBox *canGroup = new QGroupBox("CAN 设备", central);
    QHBoxLayout *canLayout = new QHBoxLayout(canGroup);

    canLayout->addWidget(new QLabel("类型:", canGroup));
    m_deviceTypeCombo = new QComboBox(canGroup);
    m_deviceTypeCombo->addItem("CANalyst-II", VCI_USBCAN2);
    m_deviceTypeCombo->setCurrentIndex(0);
    canLayout->addWidget(m_deviceTypeCombo);

    canLayout->addWidget(new QLabel("索引:", canGroup));
    m_deviceIndexSpin = new QSpinBox(canGroup);
    m_deviceIndexSpin->setRange(0, 3);
    canLayout->addWidget(m_deviceIndexSpin);

    canLayout->addWidget(new QLabel("通道:", canGroup));
    m_channelCombo = new QComboBox(canGroup);
    m_channelCombo->addItem("CAN0", 0);
    m_channelCombo->addItem("CAN1", 1);
    canLayout->addWidget(m_channelCombo);

    canLayout->addWidget(new QLabel("波特率:", canGroup));
    m_baudCombo = new QComboBox(canGroup);
    m_baudCombo->addItem("125K", 125);
    m_baudCombo->addItem("250K", 250);
    m_baudCombo->addItem("500K", 500);
    m_baudCombo->addItem("1000K", 1000);
    m_baudCombo->setCurrentIndex(2);
    canLayout->addWidget(m_baudCombo);

    m_openButton = new QPushButton("打开设备", canGroup);
    m_closeButton = new QPushButton("关闭设备", canGroup);
    canLayout->addWidget(m_openButton);
    canLayout->addWidget(m_closeButton);

    canLayout->addStretch();
    m_deviceStateLabel = new QLabel("未连接", canGroup);
    canLayout->addWidget(m_deviceStateLabel);

    mainLayout->addWidget(canGroup);

    // UDS 配置
    QGroupBox *udsGroup = new QGroupBox("UDS 服务端", central);
    QGridLayout *udsLayout = new QGridLayout(udsGroup);

    udsLayout->addWidget(new QLabel("请求 ID:", udsGroup), 0, 0);
    m_requestIdEdit = new QLineEdit("0x7DC", udsGroup);
    m_requestIdEdit->setMaximumWidth(110);
    udsLayout->addWidget(m_requestIdEdit, 0, 1);

    udsLayout->addWidget(new QLabel("响应 ID:", udsGroup), 0, 2);
    m_responseIdEdit = new QLineEdit("0x7D4", udsGroup);
    m_responseIdEdit->setMaximumWidth(110);
    udsLayout->addWidget(m_responseIdEdit, 0, 3);

    udsLayout->addWidget(new QLabel("功能 ID:", udsGroup), 0, 4);
    m_functionalIdEdit = new QLineEdit("0x7DF", udsGroup);
    m_functionalIdEdit->setMaximumWidth(110);
    udsLayout->addWidget(m_functionalIdEdit, 0, 5);

    m_extendedCheck = new QCheckBox("扩展帧 (29bit)", udsGroup);
    udsLayout->addWidget(m_extendedCheck, 0, 6, 1, 2);

    udsLayout->addWidget(new QLabel("镜像基址:", udsGroup), 1, 0);
    m_flashBaseEdit = new QLineEdit("0x80040000", udsGroup);
    udsLayout->addWidget(m_flashBaseEdit, 1, 1);

    udsLayout->addWidget(new QLabel("镜像大小:", udsGroup), 1, 2);
    m_flashSizeEdit = new QLineEdit("0x00400000", udsGroup);
    udsLayout->addWidget(m_flashSizeEdit, 1, 3);

    udsLayout->addWidget(new QLabel("镜像文件:", udsGroup), 1, 4);
    m_imagePathEdit = new QLineEdit("flash_image.bin", udsGroup);
    udsLayout->addWidget(m_imagePathEdit, 1, 5, 1, 2);
    m_browseButton = new QPushButton("浏览", udsGroup);
    udsLayout->addWidget(m_browseButton, 1, 7);

    m_startButton = new QPushButton("启动服务", udsGroup);
    m_stopButton = new QPushButton("停止服务", udsGroup);
    m_clearImageButton = new QPushButton("清空镜像", udsGroup);
    m_saveImageButton = new QPushButton("另存镜像", udsGroup);
    udsLayout->addWidget(m_startButton, 2, 0, 1, 2);
    udsLayout->addWidget(m_stopButton, 2, 2, 1, 2);
    udsLayout->addWidget(m_clearImageButton, 2, 4, 1, 1);
    udsLayout->addWidget(m_saveImageButton, 2, 5, 1, 1);

    udsLayout->addWidget(new QLabel("P2 (ms):", udsGroup), 2, 6);
    m_p2Spin = new QSpinBox(udsGroup);
    m_p2Spin->setRange(1, 65535);
    m_p2Spin->setValue(50);
    m_p2Spin->setMaximumWidth(110);
    udsLayout->addWidget(m_p2Spin, 2, 7);

    mainLayout->addWidget(udsGroup);

    // 状态与统计
    QHBoxLayout *middleLayout = new QHBoxLayout();

    QGroupBox *statusGroup = new QGroupBox("状态", central);
    QFormLayout *statusLayout = new QFormLayout(statusGroup);
    m_sessionLabel = new QLabel(sessionText(0x01), statusGroup);
    m_securityLabel = new QLabel(securityText(0), statusGroup);
    m_downloadLabel = new QLabel("空闲", statusGroup);
    m_progressBar = new QProgressBar(statusGroup);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFormat("%p%");
    m_shaLabel = new QLabel("--", statusGroup);
    m_shaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_shaLabel->setWordWrap(true);
    statusLayout->addRow("诊断会话:", m_sessionLabel);
    statusLayout->addRow("安全访问:", m_securityLabel);
    statusLayout->addRow("下载状态:", m_downloadLabel);
    statusLayout->addRow("下载进度:", m_progressBar);
    statusLayout->addRow("镜像 SHA-256:", m_shaLabel);
    middleLayout->addWidget(statusGroup, 3);

    QGroupBox *statsGroup = new QGroupBox("统计", central);
    QGridLayout *statsLayout = new QGridLayout(statsGroup);
    m_requestCountLabel = new QLabel("请求: 0", statsGroup);
    m_positiveCountLabel = new QLabel("正响应: 0", statsGroup);
    m_negativeCountLabel = new QLabel("负响应: 0", statsGroup);
    m_pendingCountLabel = new QLabel("挂起: 0", statsGroup);
    m_receiveFramesLabel = new QLabel("接收: 0 帧", statsGroup);
    m_sendFramesLabel = new QLabel("发送: 0 帧", statsGroup);
    m_frameRateLabel = new QLabel("帧率: 0.0 帧/s", statsGroup);
    statsLayout->addWidget(m_requestCountLabel, 0, 0);
    statsLayout->addWidget(m_positiveCountLabel, 0, 1);
    statsLayout->addWidget(m_negativeCountLabel, 1, 0);
    statsLayout->addWidget(m_pendingCountLabel, 1, 1);
    statsLayout->addWidget(m_receiveFramesLabel, 2, 0);
    statsLayout->addWidget(m_sendFramesLabel, 2, 1);
    statsLayout->addWidget(m_frameRateLabel, 3, 0, 1, 2);
    middleLayout->addWidget(statsGroup, 2);

    mainLayout->addLayout(middleLayout);

    // 日志
    QGroupBox *logGroup = new QGroupBox("UDS 日志", central);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

    QHBoxLayout *logToolLayout = new QHBoxLayout();
    m_clearLogButton = new QPushButton("清空", logGroup);
    m_exportLogButton = new QPushButton("导出", logGroup);
    m_autoScrollCheckBox = new QCheckBox("自动滚动", logGroup);
    m_autoScrollCheckBox->setChecked(true);
    logToolLayout->addWidget(m_clearLogButton);
    logToolLayout->addWidget(m_exportLogButton);
    logToolLayout->addWidget(m_autoScrollCheckBox);
    logToolLayout->addStretch();
    logLayout->addLayout(logToolLayout);

    m_logTable = new QTableWidget(0, 5, logGroup);
    m_logTable->setHorizontalHeaderLabels(QStringList() << "时间" << "方向" << "CAN ID" << "长度" << "数据");
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->setColumnWidth(0, 100);
    m_logTable->setColumnWidth(1, 60);
    m_logTable->setColumnWidth(2, 80);
    m_logTable->setColumnWidth(3, 50);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setAlternatingRowColors(true);
    logLayout->addWidget(m_logTable);

    mainLayout->addWidget(logGroup, 1);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
}

void MainWindow::setupConnections()
{
    connect(m_openButton, &QPushButton::clicked, this, &MainWindow::onOpenDeviceClicked);
    connect(m_closeButton, &QPushButton::clicked, this, &MainWindow::onCloseDeviceClicked);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStartServerClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::onStopServerClicked);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseImageClicked);
    connect(m_clearImageButton, &QPushButton::clicked, this, &MainWindow::onClearImageClicked);
    connect(m_saveImageButton, &QPushButton::clicked, this, &MainWindow::onSaveImageClicked);
    connect(m_clearLogButton, &QPushButton::clicked, this, &MainWindow::onClearLogClicked);
    connect(m_exportLogButton, &QPushButton::clicked, this, &MainWindow::onExportLogClicked);
    connect(m_autoScrollCheckBox, &QCheckBox::toggled, this, &MainWindow::onAutoScrollToggled);

    m_server = new UdsServer(this);
    connect(m_server, &UdsServer::udsMessage, this, &MainWindow::onUdsMessage);
    connect(m_server, &UdsServer::sessionChanged, this, &MainWindow::onSessionChanged);
    connect(m_server, &UdsServer::securityChanged, this, &MainWindow::onSecurityChanged);
    connect(m_server, &UdsServer::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(m_server, &UdsServer::statsUpdated, this, &MainWindow::onStatsUpdated);
    connect(m_server, &UdsServer::imageUpdated, this, &MainWindow::onImageUpdated);
    connect(m_server, &UdsServer::resetRequested, this, &MainWindow::onResetRequested);
    connect(m_server, &UdsServer::serverError, this, &MainWindow::onServerError);
    connect(m_server, &UdsServer::serverStateChanged, this, &MainWindow::onServerStateChanged);
}

void MainWindow::updateControlsState()
{
    m_openButton->setEnabled(!m_deviceOpened);
    m_closeButton->setEnabled(m_deviceOpened);
    m_deviceTypeCombo->setEnabled(!m_deviceOpened);
    m_deviceIndexSpin->setEnabled(!m_deviceOpened);
    m_channelCombo->setEnabled(!m_deviceOpened);
    m_baudCombo->setEnabled(!m_deviceOpened);

    m_startButton->setEnabled(m_deviceOpened && !m_serverRunning);
    m_stopButton->setEnabled(m_serverRunning);

    const bool configEnabled = !m_serverRunning;
    m_requestIdEdit->setEnabled(configEnabled);
    m_responseIdEdit->setEnabled(configEnabled);
    m_functionalIdEdit->setEnabled(configEnabled);
    m_extendedCheck->setEnabled(configEnabled);
    m_flashBaseEdit->setEnabled(configEnabled);
    m_flashSizeEdit->setEnabled(configEnabled);
    m_p2Spin->setEnabled(configEnabled);
    m_imagePathEdit->setEnabled(configEnabled);
    m_browseButton->setEnabled(configEnabled);
    m_clearImageButton->setEnabled(configEnabled);
    m_saveImageButton->setEnabled(configEnabled);
}

void MainWindow::updateStatusBar()
{
    m_statusLabel->setText(QString("CAN: %1    UDS: %2")
                           .arg(m_deviceOpened ? "已连接" : "未连接")
                           .arg(m_serverRunning ? "运行中" : "已停止"));
}

void MainWindow::onOpenDeviceClicked()
{
    if (m_deviceOpened)
    {
        return;
    }

    const UINT deviceType = static_cast<UINT>(m_deviceTypeCombo->currentData().toUInt());
    const UINT deviceIndex = static_cast<UINT>(m_deviceIndexSpin->value());
    const UINT channel = static_cast<UINT>(m_channelCombo->currentData().toUInt());
    const UINT baudRate = static_cast<UINT>(m_baudCombo->currentData().toUInt());

    if (!m_canThread)
    {
        m_canThread = new CANThread(this);
        connect(m_canThread, &CANThread::errorOccurred, this, &MainWindow::onCanError);
        m_server->setCanThread(m_canThread);
    }

    if (!m_canThread->openDevice(deviceType, deviceIndex, channel, baudRate) ||
        !m_canThread->initCAN() ||
        !m_canThread->startCAN())
    {
        m_server->setCanThread(nullptr);
        delete m_canThread;
        m_canThread = nullptr;
        m_deviceStateLabel->setText("未连接");
        updateControlsState();
        updateStatusBar();
        return;
    }

    m_canThread->start();
    m_deviceOpened = true;
    m_deviceStateLabel->setText("已连接");
    updateControlsState();
    updateStatusBar();
}

void MainWindow::onCloseDeviceClicked()
{
    if (m_serverRunning)
    {
        onStopServerClicked();
    }

    if (!m_canThread)
    {
        return;
    }

    m_server->setCanThread(nullptr);
    m_canThread->stop();
    m_canThread->wait(1000);
    delete m_canThread;
    m_canThread = nullptr;

    m_deviceOpened = false;
    m_deviceStateLabel->setText("未连接");
    updateControlsState();
    updateStatusBar();
}

void MainWindow::onStartServerClicked()
{
    if (!m_deviceOpened)
    {
        QMessageBox::warning(this, "提示", "请先打开 CAN 设备");
        return;
    }

    bool ok = true;
    UdsServer::Config config;
    config.requestId = parseUint(m_requestIdEdit->text(), 0x7E0, &ok);
    if (!ok)
    {
        QMessageBox::warning(this, "提示", "请求 ID 格式错误");
        return;
    }
    config.responseId = parseUint(m_responseIdEdit->text(), 0x7E8, &ok);
    if (!ok)
    {
        QMessageBox::warning(this, "提示", "响应 ID 格式错误");
        return;
    }
    config.functionalId = parseUint(m_functionalIdEdit->text(), 0x7DF, &ok);
    if (!ok)
    {
        QMessageBox::warning(this, "提示", "功能 ID 格式错误");
        return;
    }
    config.flashBase = parseUint(m_flashBaseEdit->text(), 0x80040000, &ok);
    if (!ok)
    {
        QMessageBox::warning(this, "提示", "镜像基址格式错误");
        return;
    }
    config.flashSize = parseUint(m_flashSizeEdit->text(), 0x00400000, &ok);
    if (!ok)
    {
        QMessageBox::warning(this, "提示", "镜像大小格式错误");
        return;
    }

    config.extendedFrame = m_extendedCheck->isChecked();
    config.p2Ms = static_cast<quint16>(m_p2Spin->value());
    config.imagePath = m_imagePathEdit->text().trimmed();

    const quint32 maxId = config.extendedFrame ? 0x1FFFFFFF : 0x7FF;
    if (config.requestId > maxId || config.responseId > maxId || config.functionalId > maxId)
    {
        QMessageBox::warning(this, "提示", config.extendedFrame ? "ID 超出 29bit 范围" : "标准帧 ID 不能超过 0x7FF");
        return;
    }

    m_server->startServer(config);
}

void MainWindow::onStopServerClicked()
{
    m_server->stopServer();
}

void MainWindow::onBrowseImageClicked()
{
    QString path = QFileDialog::getSaveFileName(this, "选择镜像文件", m_imagePathEdit->text(),
                                                "镜像文件 (*.bin);;所有文件 (*)");
    if (!path.isEmpty())
    {
        m_imagePathEdit->setText(path);
    }
}

void MainWindow::onClearImageClicked()
{
    if (m_serverRunning)
    {
        return;
    }

    bool ok = true;
    quint32 base = parseUint(m_flashBaseEdit->text(), 0x80040000, &ok);
    quint32 size = parseUint(m_flashSizeEdit->text(), 0x00400000, &ok);
    if (!ok || size < 0x1000)
    {
        QMessageBox::warning(this, "提示", "镜像基址或大小格式错误");
        return;
    }

    if (QMessageBox::question(this, "确认", "确定要将镜像全部填充为 0xFF 吗？",
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    FlashImage image;
    image.configure(base, size, m_imagePathEdit->text().trimmed());
    if (!image.open())
    {
        QMessageBox::warning(this, "提示", "打开镜像文件失败");
        return;
    }
    image.eraseAll();
    image.save();
    m_shaLabel->setText(image.getSha256());
    image.close();

    m_progressBar->setValue(0);
    m_downloadLabel->setText("空闲");
    statusBar()->showMessage("镜像已清空（0xFF）", 3000);
}

void MainWindow::onSaveImageClicked()
{
    const QString source = m_imagePathEdit->text().trimmed();
    if (source.isEmpty() || !QFile::exists(source))
    {
        QMessageBox::warning(this, "提示", "当前镜像文件不存在");
        return;
    }

    QString target = QFileDialog::getSaveFileName(this, "另存镜像", source,
                                                  "镜像文件 (*.bin);;所有文件 (*)");
    if (target.isEmpty() || target == source)
    {
        return;
    }

    if (QFile::exists(target) && !QFile::remove(target))
    {
        QMessageBox::warning(this, "提示", "无法覆盖目标文件");
        return;
    }
    if (!QFile::copy(source, target))
    {
        QMessageBox::warning(this, "提示", "保存镜像失败");
        return;
    }

    statusBar()->showMessage("镜像已另存: " + target, 3000);
}

void MainWindow::onClearLogClicked()
{
    m_pendingLog.clear();
    m_logTable->setRowCount(0);
}

void MainWindow::onExportLogClicked()
{
    if (m_logTable->rowCount() == 0)
    {
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, "导出日志", "uds_log.txt",
                                                "文本文件 (*.txt);;CSV 文件 (*.csv);;所有文件 (*)");
    if (path.isEmpty())
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, "提示", "无法写入日志文件");
        return;
    }

    QTextStream stream(&file);
    for (int row = 0; row < m_logTable->rowCount(); row++)
    {
        QStringList fields;
        for (int col = 0; col < m_logTable->columnCount(); col++)
        {
            QTableWidgetItem *item = m_logTable->item(row, col);
            fields << (item ? item->text() : QString());
        }
        stream << fields.join(", ") << '\n';
    }
    file.close();

    statusBar()->showMessage("日志已导出: " + path, 3000);
}

void MainWindow::onAutoScrollToggled(bool checked)
{
    m_autoScroll = checked;
}

void MainWindow::onFlushLog()
{
    if (m_pendingLog.isEmpty())
    {
        return;
    }

    m_logTable->setUpdatesEnabled(false);
    for (const LogEntry &entry : m_pendingLog)
    {
        appendLogRow(entry);
    }
    m_pendingLog.clear();

    m_logTable->setUpdatesEnabled(true);

    if (m_autoScroll)
    {
        m_logTable->scrollToBottom();
    }
}

void MainWindow::appendLogRow(const LogEntry &entry)
{
    const int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    const bool isEvent = entry.data.isEmpty();
    const QString direction = isEvent ? "信息" : (entry.tx ? "发送" : "接收");
    const QString canIdText = isEvent ? "--" : QString("0x%1").arg(entry.canId, 3, 16, QChar('0')).toUpper();
    const QString lengthText = QString::number((entry.data.size() + 1) / 3);
    const QString dataText = isEvent ? entry.note : entry.data;

    m_logTable->setItem(row, 0, new QTableWidgetItem(entry.time));
    m_logTable->setItem(row, 1, new QTableWidgetItem(direction));
    m_logTable->setItem(row, 2, new QTableWidgetItem(canIdText));
    m_logTable->setItem(row, 3, new QTableWidgetItem(lengthText));
    m_logTable->setItem(row, 4, new QTableWidgetItem(dataText));

    const QBrush background(entry.tx ? QColor(200, 255, 200) : QColor(200, 200, 255));
    for (int col = 0; col < m_logTable->columnCount(); col++)
    {
        if (QTableWidgetItem *item = m_logTable->item(row, col))
        {
            if (!isEvent)
            {
                item->setBackground(background);
            }
        }
    }
}

void MainWindow::onUdsMessage(bool tx, quint32 canId, QString data, QString note)
{
    LogEntry entry;
    entry.time = QTime::currentTime().toString("hh:mm:ss.zzz");
    entry.tx = tx;
    entry.canId = canId;
    entry.data = data;
    entry.note = note;
    m_pendingLog.append(entry);
}

void MainWindow::onSessionChanged(int session)
{
    m_sessionLabel->setText(sessionText(session));
}

void MainWindow::onSecurityChanged(int level)
{
    m_securityLabel->setText(securityText(level));
}

void MainWindow::onDownloadProgress(quint32 written, quint32 total)
{
    if (total == 0)
    {
        m_progressBar->setValue(0);
        m_downloadLabel->setText("空闲");
        return;
    }

    m_progressBar->setValue(static_cast<int>(static_cast<quint64>(written) * 100 / total));
    m_downloadLabel->setText(QString("%1 / %2 字节").arg(written).arg(total));
}

void MainWindow::onStatsUpdated(UdsStats stats)
{
    m_requestCountLabel->setText(QString("请求: %1").arg(stats.requestsReceived));
    m_positiveCountLabel->setText(QString("正响应: %1").arg(stats.positiveResponses));
    m_negativeCountLabel->setText(QString("负响应: %1").arg(stats.negativeResponses));
    m_pendingCountLabel->setText(QString("挂起: %1").arg(stats.pendingResponses));
    m_receiveFramesLabel->setText(QString("接收: %1 帧").arg(stats.framesReceived));
    m_sendFramesLabel->setText(QString("发送: %1 帧").arg(stats.framesSent));
    m_frameRateLabel->setText(QString("帧率: %1 帧/s").arg(stats.frameRate, 0, 'f', 1));
}

void MainWindow::onImageUpdated(QString sha256, quint32 writtenBytes)
{
    Q_UNUSED(writtenBytes)

    m_shaLabel->setText(sha256);
    m_shaLabel->setToolTip(sha256);
}

void MainWindow::onResetRequested(int resetType)
{
    LogEntry entry;
    entry.time = QTime::currentTime().toString("hh:mm:ss.zzz");
    entry.tx = false;
    entry.canId = 0;
    entry.data = QString();
    entry.note = QString("ECU 软复位，复位类型 0x%1").arg(resetType, 2, 16, QChar('0')).toUpper();
    m_pendingLog.append(entry);

    m_progressBar->setValue(0);
    m_downloadLabel->setText("空闲");
}

void MainWindow::onServerError(QString error)
{
    statusBar()->showMessage(error, 5000);
    QMessageBox::warning(this, "UDS 服务端", error);
}

void MainWindow::onServerStateChanged(bool running)
{
    m_serverRunning = running;
    updateControlsState();
    updateStatusBar();
    statusBar()->showMessage(running ? "UDS 服务端已启动" : "UDS 服务端已停止", 3000);
}

void MainWindow::onCanError(QString error)
{
    statusBar()->showMessage(error, 5000);
}

QString MainWindow::sessionText(int session)
{
    switch (session)
    {
    case 0x02: return "编程会话 (0x02)";
    case 0x03: return "扩展会话 (0x03)";
    case 0x01:
    default:   return "默认会话 (0x01)";
    }
}

QString MainWindow::securityText(int level)
{
    switch (level)
    {
    case 0x01: return "已解锁（等级 0x01）";
    case 0x03: return "已解锁（等级 0x03）";
    default:   return "锁定";
    }
}

quint32 MainWindow::parseUint(const QString &text, quint32 defaultValue, bool *ok)
{
    QString value = text.trimmed();
    if (value.startsWith("0x", Qt::CaseInsensitive))
    {
        value = value.mid(2);
    }

    bool valid = false;
    quint32 result = value.toUInt(&valid, 16);
    if (ok)
    {
        *ok = valid;
    }
    return valid ? result : defaultValue;
}
