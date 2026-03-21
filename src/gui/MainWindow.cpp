#include "MainWindow.h"
#include "CameraWidget.h"
#include "LogWidget.h"
#include "ArmWidget.h"
#include "UGVWidget.h"
#include "AirportWidget.h"
#include "GripperWidget.h"
#include "MeshMapWidget.h"
#include "DroneWidget.h"
#include "Tab2CommConfig.h"
#include "Tab3Help.h"
#include "core/RpcClient.h"
#include "core/VideoClient.h"
#include "core/Protocol.h"

#include <QTabWidget>
#include <QSplitter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QFrame>
#include <QJsonObject>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , rpc_client_(new RpcClient(this))
    , video_client_(new VideoClient(this))
{
    setWindowTitle("无人机机器人控制台");
    resize(1400, 900);

    buildUi();

    // Connect RpcClient signals
    connect(rpc_client_, &RpcClient::connected,    this, &MainWindow::onRpcConnected);
    connect(rpc_client_, &RpcClient::disconnected, this, &MainWindow::onRpcDisconnected);
    connect(rpc_client_, &RpcClient::logMessage,   this, &MainWindow::onLogMessage);

    // Connect VideoClient signals
    connect(video_client_, &VideoClient::frameReady,  camera_widget_, &CameraWidget::setFrame);
    connect(video_client_, &VideoClient::fpsUpdated,  camera_widget_, &CameraWidget::updateFps);
    connect(video_client_, &VideoClient::fpsUpdated,  this,           &MainWindow::onFpsUpdated);

    // Initial state
    btn_disconnect_->setEnabled(false);
    setLedColor("#353650");
    status_label_->setText("未连接");
}

MainWindow::~MainWindow()
{
}

void MainWindow::buildUi()
{
    tab_widget_ = new QTabWidget(this);
    setCentralWidget(tab_widget_);

    // Tab 1: Dashboard
    QWidget *dashTab = buildDashboardTab();
    tab_widget_->addTab(dashTab, "主控面板");

    // Tab 2: Interfaces
    tab2_ = new Tab2CommConfig(rpc_client_, this);
    tab_widget_->addTab(tab2_, "接口配置");

    // Tab 3: Help
    tab3_ = new Tab3Help(this);
    tab_widget_->addTab(tab3_, "帮助");

    // Status bar
    QStatusBar *sb = statusBar();

    status_label_ = new QLabel("未连接", this);
    status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    sb->addWidget(status_label_);

    sb->addPermanentWidget(new QLabel("  |  ", this));

    fps_label_ = new QLabel("帧率: --", this);
    fps_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    sb->addPermanentWidget(fps_label_);
}

QWidget* MainWindow::buildDashboardTab()
{
    auto *dashWidget = new QWidget(this);
    auto *dashLayout = new QHBoxLayout(dashWidget);
    dashLayout->setContentsMargins(6, 6, 6, 6);
    dashLayout->setSpacing(6);

    // Main horizontal splitter
    auto *mainSplitter = new QSplitter(Qt::Horizontal, dashWidget);
    dashLayout->addWidget(mainSplitter);

    // ── Left: camera + log (vertical splitter) ──
    auto *leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);

    camera_widget_ = new CameraWidget(leftSplitter);
    camera_widget_->setMinimumHeight(300);

    log_widget_ = new LogWidget(leftSplitter);
    log_widget_->setMinimumHeight(120);

    leftSplitter->addWidget(camera_widget_);
    leftSplitter->addWidget(log_widget_);
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 1);
    leftSplitter->setSizes({600, 200});

    mainSplitter->addWidget(leftSplitter);

    // ── Right: scroll area with control widgets ──
    auto *scrollArea = new QScrollArea(mainSplitter);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *rightContainer = new QWidget(scrollArea);
    rightContainer->setMinimumWidth(320);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setSpacing(8);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    // Connection group
    rightLayout->addWidget(buildConnectionGroup());

    // Sub-system widgets
    arm_widget_     = new ArmWidget(rpc_client_, rightContainer);
    ugv_widget_     = new UGVWidget(rpc_client_, rightContainer);
    airport_widget_ = new AirportWidget(rpc_client_, rightContainer);
    gripper_widget_ = new GripperWidget(rpc_client_, rightContainer);
    mesh_widget_    = new MeshMapWidget(rightContainer);
    drone_widget_   = new DroneWidget(rightContainer);

    rightLayout->addWidget(arm_widget_);
    rightLayout->addWidget(ugv_widget_);
    rightLayout->addWidget(airport_widget_);
    rightLayout->addWidget(gripper_widget_);
    rightLayout->addWidget(mesh_widget_);
    rightLayout->addWidget(drone_widget_);
    rightLayout->addStretch();

    scrollArea->setWidget(rightContainer);
    mainSplitter->addWidget(scrollArea);

    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({1000, 380});

    return dashWidget;
}

QWidget* MainWindow::buildConnectionGroup()
{
    auto *grp = new QGroupBox("连接设置", this);
    auto *layout = new QVBoxLayout(grp);
    layout->setSpacing(6);
    layout->setContentsMargins(8, 18, 8, 8);

    // Host row
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(new QLabel("主机:", grp));
    host_edit_ = new QLineEdit("192.168.10.2", grp);
    hostRow->addWidget(host_edit_);
    layout->addLayout(hostRow);

    // Ports row
    auto *portsRow = new QHBoxLayout;
    portsRow->addWidget(new QLabel("RPC端口:", grp));
    rpc_port_spin_ = new QSpinBox(grp);
    rpc_port_spin_->setRange(1, 65535);
    rpc_port_spin_->setValue(Protocol::RPC_PORT);
    rpc_port_spin_->setFixedWidth(70);
    portsRow->addWidget(rpc_port_spin_);

    portsRow->addSpacing(8);
    portsRow->addWidget(new QLabel("视频端口:", grp));
    video_port_spin_ = new QSpinBox(grp);
    video_port_spin_->setRange(1, 65535);
    video_port_spin_->setValue(Protocol::VIDEO_PORT);
    video_port_spin_->setFixedWidth(70);
    portsRow->addWidget(video_port_spin_);
    portsRow->addStretch();
    layout->addLayout(portsRow);

    // Connect/Disconnect + LED
    auto *btnRow = new QHBoxLayout;

    // Status LED
    led_label_ = new QLabel(grp);
    led_label_->setFixedSize(16, 16);
    led_label_->setStyleSheet(
        "background-color: #353650; border: 1px solid #555770; border-radius: 8px;");
    btnRow->addWidget(led_label_);

    btn_connect_    = new QPushButton("连接",    grp);
    btn_disconnect_ = new QPushButton("断开", grp);
    btn_connect_->setFixedHeight(30);
    btn_disconnect_->setFixedHeight(30);

    btnRow->addWidget(btn_connect_);
    btnRow->addWidget(btn_disconnect_);
    layout->addLayout(btnRow);

    connect(btn_connect_,    &QPushButton::clicked, this, &MainWindow::onConnect);
    connect(btn_disconnect_, &QPushButton::clicked, this, &MainWindow::onDisconnect);

    return grp;
}

void MainWindow::onConnect()
{
    QString host     = host_edit_->text().trimmed();
    quint16 rpcPort  = static_cast<quint16>(rpc_port_spin_->value());
    quint16 vidPort  = static_cast<quint16>(video_port_spin_->value());

    rpc_client_->setHost(host, rpcPort);
    video_client_->setHost(host, vidPort);

    rpc_client_->connectToHost();
    video_client_->connectToHost();

    // Mirror to Tab2
    tab2_->setConnectionParams(host, rpcPort, vidPort);

    btn_connect_->setEnabled(false);
    setLedColor("#ff9800");
    status_label_->setText("连接中...");
}

void MainWindow::onDisconnect()
{
    rpc_client_->disconnectFromHost();
    video_client_->disconnectFromHost();
}

void MainWindow::onRpcConnected()
{
    btn_connect_->setEnabled(false);
    btn_disconnect_->setEnabled(true);
    setLedColor("#4caf50");
    status_label_->setText("已连接: " + host_edit_->text());

    // Send a ping to verify
    rpc_client_->call(Protocol::Methods::SYSTEM_PING, QJsonObject{},
        [this](QJsonObject result) {
            Q_UNUSED(result)
            onPingResult();
        });
}

void MainWindow::onRpcDisconnected()
{
    btn_connect_->setEnabled(true);
    btn_disconnect_->setEnabled(false);
    setLedColor("#353650");
    status_label_->setText("未连接");
    fps_label_->setText("帧率: --");
}

void MainWindow::onFpsUpdated(double fps)
{
    fps_label_->setText(QString("帧率: %1").arg(fps, 0, 'f', 1));
}

void MainWindow::onLogMessage(const QString &msg)
{
    log_widget_->appendLogMsg(msg);
}

void MainWindow::onPingResult()
{
    log_widget_->appendLog("INFO", "[系统] Ping 成功 - 机器人在线。");
}

void MainWindow::setLedColor(const QString &color)
{
    led_label_->setStyleSheet(
        QString("background-color: %1; border: 1px solid #555770; border-radius: 8px;")
        .arg(color));
}
