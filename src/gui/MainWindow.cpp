#include "MainWindow.h"
#include "CameraWidget.h"
#include "LogWidget.h"
#include "ArmWidget.h"
#include "PiperWidget.h"
#include "UGVWidget.h"
#include "AirportWidget.h"
#include "GripperWidget.h"
#include "MeshMapWidget.h"
#include "DroneWidget.h"
#include "CalibWidget.h"
#include "TeachWidget.h"
#include "Tab2CommConfig.h"
#include "Tab3Help.h"
#include "Tab4TaskConfig.h"
#include "NpuWidget.h"
#include "core/RpcClient.h"
#include "core/VideoClient.h"
#include "core/MeshPinger.h"
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
#include <QRadioButton>
#include <QStatusBar>
#include <QFrame>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QSettings>
#include <QCloseEvent>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
// DwmSetWindowAttribute attribute IDs for the dark-mode title bar were
// renumbered between insider builds: 19 on Win10 19H1 (18985..19041 dev)
// and 20 on Win10 20H1+ / Win11. Calling both is safe — unknown IDs
// return an error code we ignore.
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_OLD
#    define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#  endif
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#  ifndef DWMWA_CAPTION_COLOR
#    define DWMWA_CAPTION_COLOR 35  // Win11 22000+
#  endif
#  ifndef DWMWA_TEXT_COLOR
#    define DWMWA_TEXT_COLOR 36
#  endif
#  ifndef DWMWA_BORDER_COLOR
#    define DWMWA_BORDER_COLOR 34
#  endif
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , rpc_client_(new RpcClient(this))
    , video_client_(new VideoClient(this))
    , mesh_pinger_(new MeshPinger(this))
{
    setWindowTitle("无人机机器人控制台");
    resize(1400, 900);

    buildUi();

#ifdef Q_OS_WIN
    // ── Dark-themed Windows title bar ────────────────────────────────────
    // The default white system caption clashes with our dark scientific
    // palette. DWM exposes a private flag (officially documented in
    // Windows SDK 10.0.22000) that flips the non-client area to dark.
    // The colour-customisation attributes (CAPTION_COLOR / TEXT_COLOR /
    // BORDER_COLOR) only exist on Win11 22H2+; older builds silently
    // ignore them.
    HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &useDark, sizeof(useDark));

    // Use the same panel color as the StyleSheet dark theme so the
    // caption blends with the rest of the window. COLORREF is 0x00BBGGRR.
    const COLORREF kCaption = 0x001A1816;  // ~ #16181A
    const COLORREF kText    = 0x00E8DCC8;  // ~ #C8DCE8
    const COLORREF kBorder  = 0x00322830;  // ~ #302832
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &kCaption, sizeof(kCaption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR,    &kText,    sizeof(kText));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR,  &kBorder,  sizeof(kBorder));
#endif

    // Connect RpcClient signals
    connect(rpc_client_, &RpcClient::connected,    this, &MainWindow::onRpcConnected);
    connect(rpc_client_, &RpcClient::disconnected, this, &MainWindow::onRpcDisconnected);
    connect(rpc_client_, &RpcClient::logMessage,   this, &MainWindow::onLogMessage);

    // Reparent the single CameraWidget when the active tab changes so the
    // video stream follows the user between 主控面板 and 任务配置 (only one
    // shows it at any moment).
    connect(tab_widget_, &QTabWidget::currentChanged,
            this,        &MainWindow::onTabChanged);

    // Connect VideoClient signals
    connect(video_client_, &VideoClient::frameReady,  camera_widget_, &CameraWidget::setFrame);
    connect(video_client_, &VideoClient::fpsUpdated,  camera_widget_, &CameraWidget::updateFps);
    connect(video_client_, &VideoClient::fpsUpdated,  this,           &MainWindow::onFpsUpdated);
    connect(video_client_, &VideoClient::logMessage,  this,           &MainWindow::onLogMessage);

    // Mesh topology: ping 192.168.1.101..106 every 5 s, update map widget
    connect(mesh_pinger_, &MeshPinger::nodesUpdated,
            mesh_widget_,  &MeshMapWidget::updateNodes);
    connect(mesh_pinger_, &MeshPinger::nodesUpdated,
            drone_widget_, &DroneWidget::updateMeshNodes);
    // Mesh-widget "仿真" button → drone-card mock telemetry (alt=0, GPS
    // clustered with decimal-place jitter, mock battery/heading/rate).
    connect(mesh_widget_, &MeshMapWidget::simulationToggled,
            drone_widget_, &DroneWidget::setSimulationTelemetry);
    mesh_pinger_->start(5000);

    // Initial state
    btn_disconnect_->setEnabled(false);
    setLedColor("#353650");
    status_label_->setText("未连接");

    // Restore previously-saved UI parameters from the persistent INI file.
    // Done last so all child widgets exist and their loadConfig() can run.
    loadConfig();

    // Mount the camera into whichever tab was restored as active. The
    // currentChanged() signal won't fire if the restored index equals the
    // already-current index, so trigger the handler explicitly once.
    onTabChanged(tab_widget_->currentIndex());
}

MainWindow::~MainWindow()
{
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Persist all UI parameters before the window goes away.
    saveConfig();

    // ── Proactive shutdown so destructors don't have to drain everything
    //    at once. Without this, the bare ~MainWindow → Qt-child-destruct
    //    chain could:
    //      1. fire one more 20 Hz pollJointsAndPose timer into a half-dead
    //         PiperWidget,
    //      2. let MeshPinger continue spawning ping() processes that
    //         block QProcess destruction,
    //      3. leave the sim worker thread still loading STL files when
    //         Tab4TaskConfig's destructor calls wait(2000) on it.
    //
    //    Stopping pieces here, in deterministic order, makes the close
    //    snappy and the process exit clean.
    if (mesh_pinger_)  mesh_pinger_->stop();
    if (det_timer_)    det_timer_->stop();
    if (video_client_) video_client_->disconnectFromHost();
    if (rpc_client_)   rpc_client_->disconnectFromHost();

    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    tab_widget_ = new QTabWidget(this);
    setCentralWidget(tab_widget_);

    // Tab 1: Dashboard
    QWidget *dashTab = buildDashboardTab();
    dashboard_tab_ = dashTab;
    tab_widget_->addTab(dashTab, "主控面板");

    // Tab 2: Task Configuration (between dashboard and comm config)
    tab4_ = new Tab4TaskConfig(rpc_client_, this);
    tab_widget_->addTab(tab4_, "任务配置");

    // Tab 3: Interfaces
    tab2_ = new Tab2CommConfig(rpc_client_, this);
    tab_widget_->addTab(tab2_, "接口配置");

    // Tab 4: Help
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
    dash_left_splitter_ = leftSplitter;   // remembered so onTabChanged can
                                          // reattach the camera here.

    camera_widget_ = new CameraWidget(leftSplitter);
    // Min-height has to fit both the spacious dashboard slot and Tab4's
    // smaller dock; pick a low floor and let the splitters drive the
    // actual size.
    camera_widget_->setMinimumHeight(120);

    log_widget_ = new LogWidget(rpc_client_, leftSplitter);
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
    rightContainer->setMinimumWidth(460);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setSpacing(8);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    // Connection group
    rightLayout->addWidget(buildConnectionGroup());

    // Sub-system widgets
    npu_widget_     = new NpuWidget(rpc_client_, rightContainer);
    arm_widget_     = new ArmWidget(rpc_client_, rightContainer);
    piper_widget_   = new PiperWidget(rpc_client_, rightContainer);
    // Default state: show Piper widget (gen-2 is now the primary backend);
    // the system.get_backend probe in onRpcConnected() flips to ArmWidget
    // only when gateway reports the legacy ZDT CAN arm. Piper's gripper
    // is built into the arm hardware, so the separate GripperWidget is
    // hidden by default too and only re-shown for the legacy path.
    arm_widget_->hide();
    ugv_widget_     = new UGVWidget(rpc_client_, rightContainer);
    airport_widget_ = new AirportWidget(rpc_client_, rightContainer);
    gripper_widget_ = new GripperWidget(rpc_client_, rightContainer);
    gripper_widget_->hide();   // Piper has built-in gripper; legacy probe re-shows
    mesh_widget_    = new MeshMapWidget(rightContainer);
    drone_widget_   = new DroneWidget(rightContainer);
    drone_widget_->setDefaultTargetHost("192.168.1.101");

    // Hand-eye calibration sits between the arm widget and the UGV widget —
    // it pairs the on-board RealSense (RK3588 side) with the Piper flange
    // and writes the result into proc_grasp's config.
    calib_widget_ = new CalibWidget(rpc_client_, rightContainer);

    // Teach-path recorder: capture J1-J6 (+ per-point gripper state) at
    // operator-driven moments, save/load as JSON, replay onto the live arm.
    // Sits next to CalibWidget — both are "training / setup" tools.
    teach_widget_ = new TeachWidget(rpc_client_, rightContainer);

    rightLayout->addWidget(npu_widget_);
    rightLayout->addWidget(arm_widget_);
    rightLayout->addWidget(piper_widget_);   // hidden by default; revealed if backend=piper
    rightLayout->addWidget(teach_widget_);   // 示教录制 / 回放
    rightLayout->addWidget(calib_widget_);   // 手眼标定
    rightLayout->addWidget(ugv_widget_);
    rightLayout->addWidget(airport_widget_);
    rightLayout->addWidget(gripper_widget_);
    rightLayout->addWidget(mesh_widget_);
    rightLayout->addWidget(drone_widget_);
    rightLayout->addStretch();

    scrollArea->setWidget(rightContainer);
    mainSplitter->addWidget(scrollArea);

    mainSplitter->setStretchFactor(0, 5);
    mainSplitter->setStretchFactor(1, 3);
    mainSplitter->setSizes({820, 560});

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
    host_edit_ = new QLineEdit("192.168.1.101", grp);
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

    auto *videoRow = new QHBoxLayout;
    video_enable_radio_ = new QRadioButton("使能视频传输", grp);
    // Default OFF to match the backend: proc_gateway only pushes MJPEG
    // once the HostGUI opens the :7002 TCP connection, and we only open
    // it when this toggle is on. Previously defaulting to true made the
    // UI lie about the real state, forcing the user to click it OFF
    // then ON to actually start the stream.
    video_enable_radio_->setChecked(false);
    videoRow->addWidget(video_enable_radio_);

    // RGB / Depth switch — only one stream is sent at a time on :7002 so
    // bandwidth stays flat. Default RGB; pressed state = depth.
    btn_video_source_ = new QPushButton("切换深度图", grp);
    btn_video_source_->setCheckable(true);
    btn_video_source_->setChecked(false);
    btn_video_source_->setFixedHeight(26);
    videoRow->addWidget(btn_video_source_);

    videoRow->addStretch();
    layout->addLayout(videoRow);

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
    connect(video_enable_radio_, &QRadioButton::toggled, this, &MainWindow::onVideoEnabledToggled);
    connect(btn_video_source_,   &QPushButton::clicked, this, &MainWindow::onVideoSourceToggled);

    return grp;
}

void MainWindow::onConnect()
{
    QString host     = host_edit_->text().trimmed();
    quint16 rpcPort  = static_cast<quint16>(rpc_port_spin_->value());
    quint16 vidPort  = static_cast<quint16>(video_port_spin_->value());

    rpc_client_->setHost(host, rpcPort);
    video_client_->setHost(host, vidPort);
    drone_widget_->setDefaultTargetHost(host);

    rpc_client_->connectToHost();

    // Mirror to Tab2 / Tab4
    tab2_->setConnectionParams(host, rpcPort, vidPort);
    tab4_->setConnectionParams(host, rpcPort, vidPort);

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

    // ── Backend probe: decide arm widget to display ──────────────────
    // The gateway forwards system.get_backend → proc_piper (gen-2) or
    // returns {"backend":"legacy"} for the old proc_arm path. We swap
    // the visible arm widget accordingly. Default state (set in
    // buildDashboardTab) shows PiperWidget — gen-2 is the primary backend
    // now — and we only fall back to ArmWidget on an explicit "legacy"
    // response. Anything else (probe failure, unknown backend, gateway
    // not yet upgraded to expose get_backend) keeps the default Piper UI,
    // matching what the field deployment actually runs.
    rpc_client_->call(Protocol::Methods::SYSTEM_GET_BACKEND, QJsonObject{},
        [this](QJsonObject reply) {
            const QString backend = reply.value("backend").toString("piper").toLower();
            const bool is_legacy = (backend == "legacy");
            if (is_legacy) {
                if (piper_widget_)   piper_widget_->hide();
                if (arm_widget_)     arm_widget_->show();
                if (gripper_widget_) gripper_widget_->show();
                if (arm_widget_)     arm_widget_->pushSpeeds();
                if (log_widget_) onLogMessage("[backend] legacy proc_arm detected, showing ArmWidget");
            } else {
                if (arm_widget_)     arm_widget_->hide();
                if (gripper_widget_) gripper_widget_->hide();   // Piper has built-in gripper
                if (piper_widget_) {
                    piper_widget_->show();
                    piper_widget_->onRpcConnected();
                }
                if (log_widget_) onLogMessage("[backend] gen-2 piper (default), showing PiperWidget");
            }
        });

    // Start polling NPU detections so the camera view shows bounding boxes.
    if (det_timer_ == nullptr) {
        det_timer_ = new QTimer(this);
        det_timer_->setInterval(300);   // 3-4 Hz polling — matches typical detector rate
        connect(det_timer_, &QTimer::timeout, this, &MainWindow::pollDetections);
    }
    det_timer_->start();

    // Apply the current video-toggle state now that RPC is up. The
    // toggled() signal does nothing before RPC is connected (see
    // onVideoEnabledToggled's early-return), so we explicitly replay it
    // here — otherwise a persisted-on toggle would just sit there and
    // the user would have to re-click it to actually start the stream.
    if (video_enable_radio_ && video_enable_radio_->isChecked()) {
        onVideoEnabledToggled(true);
    }

    // Re-assert the RGB/Depth source — gateway resets to RGB on each
    // (re)start, so if the UI remembers "depth" we push it back.
    if (video_source_depth_) {
        QJsonObject params;
        params["source"] = "depth";
        rpc_client_->call(Protocol::Methods::VIDEO_SET_SOURCE, params,
            [](QJsonObject) {});
    }

    // Probe sub-process health via gateway-forwarded pings.
    // Each ping tests whether the corresponding proc_* daemon is reachable.
    struct ServiceProbe {
        const char *method;   // ping routed through proc_gateway
        const char *label;
    };
    static const ServiceProbe probes[] = {
        {"arm.get_speeds",      "proc_arm"},
        {"car.set_velocity",    "proc_car"},      // params missing → ok:false but no conn error
    };

    // Use system.ping first to confirm gateway itself
    auto *results = new QStringList();
    auto *remaining = new int(0);

    // For each service, send a lightweight RPC. If the gateway can reach
    // the sub-process socket the call succeeds; otherwise it returns an
    // error or connection-refused which we surface to the user.
    for (const auto &p : probes) {
        (*remaining)++;
        QString svc = QString::fromUtf8(p.label);
        rpc_client_->call(
            QString::fromUtf8(p.method), QJsonObject(),
            [this, results, remaining, svc](QJsonObject reply) {
                bool ok = reply.value("ok").toBool(false);
                if (!ok && reply.contains("error")) {
                    *results << (svc + ": offline");
                }
                if (--(*remaining) == 0) {
                    if (!results->isEmpty()) {
                        QString msg = results->join(", ");
                        status_label_->setText("已连接: " + host_edit_->text() +
                                               "  |  " + msg);
                        log_widget_->appendLogMsg("[WARN] " + msg);
                    }
                    delete results;
                    delete remaining;
                }
            });
    }
}

void MainWindow::onRpcDisconnected()
{
    btn_connect_->setEnabled(true);
    btn_disconnect_->setEnabled(false);
    setLedColor("#353650");
    status_label_->setText("未连接");
    fps_label_->setText("帧率: --");

    if (det_timer_) det_timer_->stop();
    camera_widget_->setDetections({});

    if (piper_widget_) piper_widget_->onRpcDisconnected();
}

void MainWindow::pollDetections()
{
    if (!rpc_client_->isConnected()) return;
    rpc_client_->call("npu.get_detections", QJsonObject(),
        [this](QJsonObject reply) {
            QVector<DetectionBox> out;
            QJsonArray arr = reply.value("detections").toArray();
            out.reserve(arr.size());
            for (const auto &v : arr) {
                QJsonObject o = v.toObject();
                DetectionBox d;
                d.class_id = o.value("class_id").toInt();
                d.score    = (float)o.value("score").toDouble();
                d.x1       = (float)o.value("x1").toDouble();
                d.y1       = (float)o.value("y1").toDouble();
                d.x2       = (float)o.value("x2").toDouble();
                d.y2       = (float)o.value("y2").toDouble();
                d.x_mm     = (float)o.value("x_mm").toDouble();
                d.y_mm     = (float)o.value("y_mm").toDouble();
                d.z_mm     = (float)o.value("z_mm").toDouble();
                d.yaw_deg  = (float)o.value("yaw_deg").toDouble();
                d.has_xyz  = o.value("has_xyz").toInt() != 0;
                d.has_rpy  = o.value("has_rpy").toInt() != 0;
                out.push_back(d);
            }
            camera_widget_->setDetections(out);
        });
}

void MainWindow::onFpsUpdated(double fps)
{
    fps_label_->setText(QString("帧率: %1").arg(fps, 0, 'f', 1));
}

void MainWindow::onLogMessage(const QString &msg)
{
    log_widget_->appendLogMsg(msg);
}

void MainWindow::onTabChanged(int index)
{
    if (!camera_widget_) return;
    QWidget *active = tab_widget_->widget(index);
    if (active == tab4_ && tab4_) {
        // Move camera onto the task-config dock.
        tab4_->mountCamera(camera_widget_);
    } else if (active == dashboard_tab_ && dash_left_splitter_) {
        // Back home on the dashboard splitter — re-insert at index 0
        // (above the log) so the original layout is restored.
        if (camera_widget_->parentWidget() != dash_left_splitter_) {
            camera_widget_->setParent(dash_left_splitter_);
            dash_left_splitter_->insertWidget(0, camera_widget_);
            dash_left_splitter_->setSizes({600, 200});
            camera_widget_->show();
        }
    }
    // Other tabs (接口配置 / 帮助): leave the camera wherever it last sat —
    // the user will move it again by clicking back into 主控 or 任务配置.
}

void MainWindow::setLedColor(const QString &color)
{
    led_label_->setStyleSheet(
        QString("background-color: %1; border: 1px solid #555770; border-radius: 8px;")
        .arg(color));
}

void MainWindow::onVideoEnabledToggled(bool checked)
{
    if (!rpc_client_ || !rpc_client_->isConnected()) {
        if (!checked) {
            video_client_->disconnectFromHost();
            fps_label_->setText("帧率: --");
        }
        return;
    }

    QJsonObject params;
    params[Protocol::Fields::ENABLED] = checked;
    rpc_client_->call(Protocol::Methods::VIDEO_SET_ENABLED, params,
        [this, checked](QJsonObject) {
            if (checked) {
                log_widget_->appendLog("INFO", "[视频] 视频传输已使能。");
                video_client_->connectToHost();
            } else {
                log_widget_->appendLog("INFO", "[视频] 视频传输已关闭。");
                video_client_->disconnectFromHost();
                fps_label_->setText("帧率: --");
            }
        });
}

void MainWindow::onVideoSourceToggled()
{
    // The button is checkable — Qt flipped its state before this slot,
    // so isChecked() already reflects what the user asked for.
    video_source_depth_ = btn_video_source_->isChecked();
    btn_video_source_->setText(video_source_depth_ ? "切换RGB图" : "切换深度图");

    if (!rpc_client_ || !rpc_client_->isConnected()) {
        // No connection yet: the GUI state flips locally; the gateway
        // switch fires on the next RPC (re)connect (see onRpcConnected).
        return;
    }

    QJsonObject params;
    params["source"] = video_source_depth_ ? "depth" : "rgb";
    rpc_client_->call(Protocol::Methods::VIDEO_SET_SOURCE, params,
        [this](QJsonObject) {
            log_widget_->appendLog(
                "INFO",
                video_source_depth_
                    ? QStringLiteral("[视频] 已切换到深度图（伪彩）。")
                    : QStringLiteral("[视频] 已切换到 RGB 图像。"));
        });
}

void MainWindow::loadConfig()
{
    QSettings s;

    // ----- MainWindow's own params -----
    s.beginGroup("MainWindow");
    const QByteArray geo = s.value("geometry").toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
    const QByteArray state = s.value("state").toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    host_edit_->setText(s.value("host", host_edit_->text()).toString());
    rpc_port_spin_->setValue(s.value("rpc_port", rpc_port_spin_->value()).toInt());
    video_port_spin_->setValue(s.value("video_port", video_port_spin_->value()).toInt());
    video_enable_radio_->setChecked(
        s.value("video_enabled", video_enable_radio_->isChecked()).toBool());
    video_source_depth_ = s.value("video_source_depth", false).toBool();
    if (btn_video_source_) {
        btn_video_source_->setChecked(video_source_depth_);
        btn_video_source_->setText(video_source_depth_ ? "切换RGB图" : "切换深度图");
    }
    if (tab_widget_) {
        tab_widget_->setCurrentIndex(s.value("current_tab", 0).toInt());
    }
    s.endGroup();

    // ----- Delegate to children that know how to persist themselves -----
    if (tab2_) tab2_->loadConfig(s);
    if (arm_widget_)   arm_widget_->loadConfig(s);
    if (piper_widget_) piper_widget_->loadConfig(s);
    if (ugv_widget_)   ugv_widget_->loadConfig(s);
    if (npu_widget_)   npu_widget_->loadConfig(s);
}

void MainWindow::saveConfig() const
{
    QSettings s;

    s.beginGroup("MainWindow");
    s.setValue("geometry", saveGeometry());
    s.setValue("state", saveState());
    s.setValue("host", host_edit_->text());
    s.setValue("rpc_port", rpc_port_spin_->value());
    s.setValue("video_port", video_port_spin_->value());
    s.setValue("video_enabled", video_enable_radio_->isChecked());
    s.setValue("video_source_depth", video_source_depth_);
    if (tab_widget_) {
        s.setValue("current_tab", tab_widget_->currentIndex());
    }
    s.endGroup();

    if (tab2_) tab2_->saveConfig(s);
    if (arm_widget_)   arm_widget_->saveConfig(s);
    if (piper_widget_) piper_widget_->saveConfig(s);
    if (ugv_widget_)   ugv_widget_->saveConfig(s);
    if (npu_widget_)   npu_widget_->saveConfig(s);

    s.sync();
}
