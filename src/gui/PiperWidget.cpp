#include "PiperWidget.h"

#include "ArmViewer3D.h"
#include "ArmSyncWorker.h"
#include "../core/Protocol.h"
#include "../core/RpcClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QVector3D>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QComboBox>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QJsonValue>
#include <QFrame>
#include <QStringList>
#include <QSizePolicy>

namespace {

// Piper joint hardware limits (deg). Must match proc_piper.py's table.
struct JointLimit { double lo, hi; };
constexpr std::array<JointLimit, 6> JOINT_LIMITS = {{
    {-150.0, +150.0},
    {   0.0, +180.0},
    {-170.0,    0.0},
    {-100.0, +100.0},
    { -70.0,  +70.0},
    {-180.0, +180.0},
}};

constexpr double GRIPPER_MIN_MM = 0.0;
constexpr double GRIPPER_MAX_MM = 80.0;

constexpr int JOINT_SCALE = 100;   // slider int unit = 0.01°

}  // namespace


// ════════════════════════════════════════════════════════════════════════
// Construction
// ════════════════════════════════════════════════════════════════════════
PiperWidget::PiperWidget(RpcClient *rpc, QWidget *parent)
    : QWidget(parent), rpc_(rpc) {
    buildLayout();
    connectSignals();

    poll_state_timer_ = new QTimer(this);
    poll_state_timer_->setInterval(25);          // 40 Hz (proc_piper refreshes
                                                 // its joint/pose cache at
                                                 // 100 Hz, so going below 25 ms
                                                 // would not see fresher data)
    connect(poll_state_timer_, &QTimer::timeout, this, &PiperWidget::pollJointsAndPose);

    poll_status_timer_ = new QTimer(this);
    poll_status_timer_->setInterval(500);        // 2 Hz
    connect(poll_status_timer_, &QTimer::timeout, this, &PiperWidget::pollStatus);
}

PiperWidget::~PiperWidget()
{
    // Stop all polling timers up-front so no new RPC callbacks queue while
    // we're tearing down. Without this the 20 Hz pollJointsAndPose timer
    // can fire one more time mid-destruction and reference a dangling
    // sim_thread_.
    if (poll_state_timer_)  poll_state_timer_->stop();
    if (poll_status_timer_) poll_status_timer_->stop();

    if (sim_thread_) {
        sim_thread_->quit();
        if (!sim_thread_->wait(5000)) {
            // Worker stuck (typically still parsing STLs). Force-exit so
            // the application can close — minor leak is preferable to a
            // hung process.
            sim_thread_->terminate();
            sim_thread_->wait(500);
        }
    }
}


// ════════════════════════════════════════════════════════════════════════
// Layout assembly
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::buildLayout() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── top bar ───────────────────────────────────────────────────────
    auto *top_frame = new QFrame(this);
    top_frame->setFrameShape(QFrame::StyledPanel);
    auto *top_layout = new QHBoxLayout(top_frame);
    top_layout->setContentsMargins(8, 4, 8, 4);
    top_layout->setSpacing(10);

    conn_led_ = new QLabel("● 未连接", top_frame);
    conn_led_->setStyleSheet("color:#c44; font-weight:bold;");
    top_layout->addWidget(conn_led_);

    top_layout->addSpacing(20);
    top_layout->addWidget(new QLabel("速度:", top_frame));
    speed_slider_ = new QSlider(Qt::Horizontal, top_frame);
    speed_slider_->setRange(1, 100);
    speed_slider_->setValue(30);
    speed_slider_->setFixedWidth(160);
    top_layout->addWidget(speed_slider_);
    speed_spin_ = new QSpinBox(top_frame);
    speed_spin_->setRange(1, 100);
    speed_spin_->setValue(30);
    speed_spin_->setSuffix("%");
    speed_spin_->setFixedWidth(70);
    top_layout->addWidget(speed_spin_);

    top_layout->addStretch(1);

    enable_btn_ = new QPushButton("使能 / 恢复", top_frame);
    enable_btn_->setToolTip(
        QStringLiteral("重新执行 V1.8-2 握手 (Resume → MasterSlave → STANDBY → CAN_CTRL → Enable)。\n"
                       "撞机 / 急停 / 拖动示教退出后, 如果状态栏卡在 STANDBY 出不来 — 点这个。"));
    enable_btn_->setStyleSheet("QPushButton { background:#3a8; color:white; font-weight:bold; padding:4px 14px; }");
    top_layout->addWidget(enable_btn_);

    home_btn_ = new QPushButton("回零", top_frame);
    home_btn_->setStyleSheet("QPushButton { padding:4px 12px; }");
    top_layout->addWidget(home_btn_);

    // No software 示教 toggle: Piper V1.8-2 firmware doesn't expose a CAN
    // command that enters the real ctrl_mode=0x02 TEACHING state with
    // gravity comp — only the physical drag-teach button on the arm body
    // can trigger that. The status bar still surfaces TEACHING when the
    // operator presses the physical button, so HostGUI shows ground truth
    // either way. Tested SDK paths: MotionCtrl_1, MasterSlaveConfig,
    // DisableArm, raw 0x151 with ctrl_mode=0x02 — none of them flip the
    // firmware into the correct mode.

    estop_btn_ = new QPushButton("急停", top_frame);
    estop_btn_->setStyleSheet("QPushButton { background:#c33; color:white; font-weight:bold; padding:4px 14px; }");
    top_layout->addWidget(estop_btn_);

    root->addWidget(top_frame);

    // ── middle: splitter [tabs | 3D viewer + readouts] ────────────────
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    tabs_ = new QTabWidget(splitter);
    buildJointTab();
    buildCartesianTab();
    buildGripperTab();
    splitter->addWidget(tabs_);

    buildRightPanel();
    splitter->addWidget(viewer_3d_->parentWidget() ? viewer_3d_->parentWidget() : viewer_3d_);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, /*stretch=*/1);

    buildStatusBar();
}

void PiperWidget::buildJointTab() {
    auto *tab = new QWidget(tabs_);
    auto *grid = new QGridLayout(tab);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(10);

    grid->addWidget(new QLabel("<b>关节</b>", tab), 0, 0);
    grid->addWidget(new QLabel("<b>滑条</b>", tab), 0, 1);
    grid->addWidget(new QLabel("<b>目标 (°)</b>", tab), 0, 2);
    grid->addWidget(new QLabel("<b>限位</b>", tab), 0, 3);

    const QStringList names = {"J1 底座", "J2 肩部", "J3 肘部", "J4 手腕 R", "J5 手腕 P", "J6 末端"};
    for (int i = 0; i < 6; ++i) {
        const auto lim = JOINT_LIMITS[i];

        grid->addWidget(new QLabel(names[i], tab), i + 1, 0);

        joint_sliders_[i] = new QSlider(Qt::Horizontal, tab);
        joint_sliders_[i]->setRange(int(lim.lo * JOINT_SCALE), int(lim.hi * JOINT_SCALE));
        joint_sliders_[i]->setSingleStep(JOINT_SCALE);     // 1 deg
        joint_sliders_[i]->setPageStep(JOINT_SCALE * 5);
        joint_sliders_[i]->setValue(0);
        joint_sliders_[i]->setProperty("jointIdx", i);
        grid->addWidget(joint_sliders_[i], i + 1, 1);

        joint_spins_[i] = new QDoubleSpinBox(tab);
        joint_spins_[i]->setRange(lim.lo, lim.hi);
        joint_spins_[i]->setDecimals(2);
        joint_spins_[i]->setSingleStep(1.0);
        joint_spins_[i]->setSuffix("°");
        joint_spins_[i]->setProperty("jointIdx", i);
        grid->addWidget(joint_spins_[i], i + 1, 2);

        grid->addWidget(new QLabel(QString("[%1, %2]")
                                       .arg(lim.lo, 0, 'f', 0)
                                       .arg(lim.hi, 0, 'f', 0), tab),
                        i + 1, 3);
    }

    auto *btn_row = new QHBoxLayout;
    send_joints_btn_ = new QPushButton("发送目标", tab);
    send_joints_btn_->setToolTip("一次性把上面 6 个目标角度发给机械臂");
    btn_row->addWidget(send_joints_btn_);
    joints_zero_btn_ = new QPushButton("归零目标", tab);
    joints_zero_btn_->setToolTip("把界面上的 6 个目标全部清零 (不会立刻运动)");
    btn_row->addWidget(joints_zero_btn_);
    btn_row->addStretch(1);
    grid->addLayout(btn_row, 7, 0, 1, 4);

    grid->setRowStretch(8, 1);
    tabs_->addTab(tab, "关节控制");
}

void PiperWidget::buildCartesianTab() {
    auto *tab = new QWidget(tabs_);
    auto *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(8, 8, 8, 8);

    auto *mode_row = new QHBoxLayout;
    mode_row->addWidget(new QLabel("运动模式:", tab));
    auto *mode_box = new QComboBox(tab);
    mode_box->addItem("点位运动 (MOVE_P)");
    mode_box->addItem("直线运动 (MOVE_L)");
    mode_box->setCurrentIndex(0);
    connect(mode_box, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PiperWidget::onCartesianModeChanged);
    mode_row->addWidget(mode_box);
    mode_row->addStretch(1);
    vbox->addLayout(mode_row);

    // grid: axis | target | delta | + | -
    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel("<b>轴</b>", tab), 0, 0);
    grid->addWidget(new QLabel("<b>目标</b>", tab), 0, 1);
    grid->addWidget(new QLabel("<b>步长</b>", tab), 0, 2);
    grid->addWidget(new QLabel("<b>−</b>", tab), 0, 3);
    grid->addWidget(new QLabel("<b>+</b>", tab), 0, 4);

    const QStringList axis_names = {"X (mm)", "Y (mm)", "Z (mm)", "RX (°)", "RY (°)", "RZ (°)"};
    for (int i = 0; i < 6; ++i) {
        grid->addWidget(new QLabel(axis_names[i], tab), i + 1, 0);

        cart_spins_[i] = new QDoubleSpinBox(tab);
        cart_spins_[i]->setRange(-9999.0, +9999.0);
        cart_spins_[i]->setDecimals(2);
        cart_spins_[i]->setValue(0.0);
        grid->addWidget(cart_spins_[i], i + 1, 1);

        cart_deltas_[i] = new QDoubleSpinBox(tab);
        cart_deltas_[i]->setRange(0.1, 100.0);
        cart_deltas_[i]->setDecimals(1);
        cart_deltas_[i]->setValue(i < 3 ? 10.0 : 5.0);   // mm vs deg defaults
        grid->addWidget(cart_deltas_[i], i + 1, 2);

        auto *minus_btn = new QPushButton("−", tab);
        minus_btn->setFixedWidth(36);
        minus_btn->setProperty("axisIdx", i);
        minus_btn->setProperty("direction", -1);
        connect(minus_btn, &QPushButton::clicked, this, [this, i]() { onJogClicked(i, -1); });
        grid->addWidget(minus_btn, i + 1, 3);

        auto *plus_btn = new QPushButton("+", tab);
        plus_btn->setFixedWidth(36);
        connect(plus_btn, &QPushButton::clicked, this, [this, i]() { onJogClicked(i, +1); });
        grid->addWidget(plus_btn, i + 1, 4);
    }
    vbox->addLayout(grid);

    auto *btn_row = new QHBoxLayout;
    auto *capture_btn = new QPushButton("读取当前位姿", tab);
    capture_btn->setToolTip("把机械臂当前 X/Y/Z/RX/RY/RZ 读出来填进上面");
    connect(capture_btn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 6; ++i) cart_spins_[i]->setValue(live_pose_[i]);
    });
    btn_row->addWidget(capture_btn);

    send_cart_btn_ = new QPushButton("发送目标位姿", tab);
    btn_row->addWidget(send_cart_btn_);

    // 🎯 定点测试 — TCP 钉在当前 (X,Y,Z), RY 来回扫 ±15°, 看关节
    // 重新分配但 TCP 点不动. 没数据时先 "读取当前位姿".
    auto *fix_btn = new QPushButton(QStringLiteral("🎯 定点测试"), tab);
    fix_btn->setToolTip(
        QStringLiteral("TCP 锁在上面的 (X,Y,Z), RY 在 ±15° 之间来回扫 4 次.\n"
                       "观察关节角度在变但 TCP 锁定 — Cartesian 控制正常的信号."));
    fix_btn->setStyleSheet(
        "QPushButton{ background:#357ec7; color:white; font-weight:bold;"
        " padding:4px 12px; border-radius:4px; }"
        "QPushButton:hover{ background:#4689d8; }"
        "QPushButton:disabled{ background:#446; color:#aab; }");
    connect(fix_btn, &QPushButton::clicked, this, [this, fix_btn]() {
        if (!rpc_ || !rpc_->isConnected()) {
            qWarning("[piper] 定点测试: RPC 未连接");
            return;
        }
        // Always pull live pose first — operator might not have clicked
        // 读取当前位姿. Without this, sending {X:0,Y:0,Z:0} = arm base, IK
        // refuses, backend silently rejects, button looks broken.
        fix_btn->setEnabled(false);
        fix_btn->setText(QStringLiteral("🎯 读取当前位姿..."));

        // Error logger for the actual move calls — surface anything the
        // backend rejects (e.g. arm in STANDBY after collision).
        auto err_cb = [](QJsonObject reply) {
            if (reply.contains("error")) {
                qWarning("[piper] 定点测试 RPC 错误: %s",
                    qPrintable(reply.value("error").toString()));
            }
        };

        rpc_->call(QStringLiteral("arm.get_pose"), QJsonObject{},
            [this, fix_btn, err_cb](QJsonObject reply) {
                // proc_piper returns arm.get_pose as a bare list (List[float]),
                // not a dict — RpcClient now exposes it under "_array".
                // Fall back to legacy named-keys form for older backends.
                double v[6] = {0,0,0,0,0,0};
                QJsonArray arr = reply.value("_array").toArray();
                if (arr.size() != 6) arr = reply.value("pose").toArray();
                if (arr.size() == 6) {
                    for (int i = 0; i < 6; ++i) v[i] = arr[i].toDouble();
                } else {
                    v[0] = reply.value("x_mm").toDouble();
                    v[1] = reply.value("y_mm").toDouble();
                    v[2] = reply.value("z_mm").toDouble();
                    v[3] = reply.value("roll_deg").toDouble();
                    v[4] = reply.value("pitch_deg").toDouble();
                    v[5] = reply.value("yaw_deg").toDouble();
                }
                if (std::abs(v[0]) + std::abs(v[1]) + std::abs(v[2]) < 1.0) {
                    qWarning("[piper] 定点测试: arm.get_pose 返回 (0,0,0), 臂可能未在 CAN_CTRL — 取消");
                    fix_btn->setText(QStringLiteral("🎯 定点测试 (取消: 无位姿)"));
                    QTimer::singleShot(2000, fix_btn, [fix_btn]() {
                        fix_btn->setText(QStringLiteral("🎯 定点测试"));
                        fix_btn->setEnabled(true);
                    });
                    return;
                }
                // Reflect the fetched pose into the editor spinboxes so the
                // operator can see what point we're locking to.
                for (int i = 0; i < 6; ++i) cart_spins_[i]->setValue(v[i]);

                fix_btn->setText(QStringLiteral("🎯 摆姿态中..."));
                const double x  = v[0], y = v[1], z = v[2];
                const double rx = v[3], ry0 = v[4], rz = v[5];
                const QVector<double> ry_seq = { ry0 + 15.0, ry0 - 15.0,
                                                  ry0 + 15.0, ry0 };
                constexpr int kStepMs = 1200;
                for (int i = 0; i < ry_seq.size(); ++i) {
                    QTimer::singleShot(i * kStepMs, this,
                        [this, x, y, z, rx, ry = ry_seq[i], rz, err_cb]() {
                            if (!rpc_) return;
                            // mode = "L" (笛卡尔直线插值) 关键!
                            // 起点终点 X/Y/Z 相同时, Cartesian linear 路径
                            // 长度为 0, TCP 全程钉在 (x,y,z), 只有姿态在
                            // slerp. 用 "P" (关节插值) 的话, joints 直线
                            // 过去过程中 TCP 会绕一道弧.
                            QJsonObject p;
                            p["X_mm"]   = x;  p["Y_mm"]   = y;  p["Z_mm"]   = z;
                            p["RX_deg"] = rx; p["RY_deg"] = ry; p["RZ_deg"] = rz;
                            p["mode"]   = "L";
                            rpc_->call(QStringLiteral("piper.move_cartesian"),
                                       p, err_cb);
                        });
                }
                QTimer::singleShot(ry_seq.size() * kStepMs + 500, fix_btn,
                    [fix_btn]() {
                        fix_btn->setText(QStringLiteral("🎯 定点测试"));
                        fix_btn->setEnabled(true);
                    });
            });
    });
    btn_row->addWidget(fix_btn);

    btn_row->addStretch(1);
    vbox->addLayout(btn_row);

    vbox->addStretch(1);
    tabs_->addTab(tab, "笛卡尔运动");
}

void PiperWidget::buildGripperTab() {
    auto *tab = new QWidget(tabs_);
    auto *vbox = new QVBoxLayout(tab);
    vbox->setContentsMargins(8, 8, 8, 8);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel("夹爪开度:", tab));

    gripper_slider_ = new QSlider(Qt::Horizontal, tab);
    gripper_slider_->setRange(0, int(GRIPPER_MAX_MM * 100));
    gripper_slider_->setValue(int(GRIPPER_MAX_MM * 100));
    row->addWidget(gripper_slider_);

    gripper_spin_ = new QDoubleSpinBox(tab);
    gripper_spin_->setRange(GRIPPER_MIN_MM, GRIPPER_MAX_MM);
    gripper_spin_->setDecimals(2);
    gripper_spin_->setSuffix(" mm");
    gripper_spin_->setValue(GRIPPER_MAX_MM);
    row->addWidget(gripper_spin_);
    vbox->addLayout(row);

    auto *btn_row = new QHBoxLayout;
    gripper_open_btn_  = new QPushButton("张开", tab);
    gripper_close_btn_ = new QPushButton("闭合", tab);
    btn_row->addWidget(gripper_open_btn_);
    btn_row->addWidget(gripper_close_btn_);
    btn_row->addStretch(1);
    vbox->addLayout(btn_row);

    vbox->addWidget(new QLabel("<i>滑动停止时发送目标; 闭合 = 0mm, 张开 = "
                               + QString::number(GRIPPER_MAX_MM, 'f', 0) + "mm.</i>", tab));
    vbox->addStretch(1);
    tabs_->addTab(tab, "夹爪");
}

void PiperWidget::buildRightPanel() {
    auto *right = new QWidget(this);
    auto *vbox = new QVBoxLayout(right);
    vbox->setContentsMargins(4, 0, 0, 0);

    viewer_3d_ = new ArmViewer3D(right);
    viewer_3d_->setMinimumHeight(280);
    vbox->addWidget(viewer_3d_, /*stretch=*/2);

    // ── Spin up ArmSyncWorker just for asset loading ──────────────────
    // Reads assets/arm_model/arm_model.json + the 10 Piper STLs off the
    // GUI thread (≈9 MB total), then emits configReady / meshReady to
    // populate viewer_3d_. We don't call startAnglePolling — joint
    // angles are pushed in directly from pollJointsAndPose() below.
    sim_thread_ = new QThread(this);
    sim_thread_->setObjectName("PiperSim");
    sim_worker_ = new ArmSyncWorker();
    sim_worker_->moveToThread(sim_thread_);
    connect(sim_thread_, &QThread::finished, sim_worker_, &QObject::deleteLater);

    connect(sim_worker_, &ArmSyncWorker::configReady, this,
            [this](const QVector<QVector3D> &axes,
                   const QVector<QVector3D> &origins,
                   const QVector3D           &center,
                   float                      radius) {
                ArmViewer3D::Config cfg;
                cfg.joint_axes    = axes;
                cfg.joint_origins = origins;
                cfg.scene_center  = center;
                cfg.scene_radius  = radius;
                if (viewer_3d_) viewer_3d_->setConfig(cfg);
            });
    connect(sim_worker_, &ArmSyncWorker::meshReady,
            viewer_3d_,   &ArmViewer3D::addMeshCPU);

    sim_thread_->start();

    // Try the deployed location first (next to the exe), then fall back
    // to the source tree so dev builds straight from build_win/ also work.
    const QString exeDir = QCoreApplication::applicationDirPath();
    QDir          d(exeDir);
    QString       model_dir = d.absoluteFilePath("assets/arm_model");
    if (!QDir(model_dir).exists("arm_model.json")) {
        d.cdUp(); d.cdUp();
        model_dir = d.absoluteFilePath("assets/arm_model");
    }
    QMetaObject::invokeMethod(sim_worker_, "loadAssets",
                              Qt::QueuedConnection,
                              Q_ARG(QString, model_dir));

    // The pose + joint readouts that used to live in a "实时反馈" group
    // here are now drawn directly on top of the 3D viewer (HUD overlay
    // — see ArmViewer3D::setEndEffectorPose + paintGL), which means the
    // dashboard real estate that group occupied is freed up and the X/Y/Z
    // numbers sit right next to the model where they belong. The QLabel
    // pointers stay null so any leftover updater calls are skipped via
    // the early-return guards in updatePoseReadouts / updateJointReadouts.
    right->setMinimumWidth(280);
}

void PiperWidget::buildStatusBar() {
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(8, 2, 8, 2);

    status_ctrl_      = new QLabel("控制: —", frame);
    status_arm_       = new QLabel("状态: —", frame);
    status_motion_    = new QLabel("运动: —", frame);
    status_heartbeat_ = new QLabel("心跳: —", frame);
    for (QLabel *l : {status_ctrl_, status_arm_, status_motion_, status_heartbeat_}) {
        l->setStyleSheet("font-family:monospace;");
    }

    layout->addWidget(status_ctrl_);
    layout->addSpacing(20);
    layout->addWidget(status_arm_);
    layout->addSpacing(20);
    layout->addWidget(status_motion_);
    layout->addSpacing(20);
    layout->addWidget(status_heartbeat_);
    layout->addStretch(1);

    auto *root = qobject_cast<QVBoxLayout*>(this->layout());
    if (root) root->addWidget(frame);
}


// ════════════════════════════════════════════════════════════════════════
// Signal wiring
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::connectSignals() {
    // top bar
    connect(speed_slider_, &QSlider::valueChanged, speed_spin_, &QSpinBox::setValue);
    connect(speed_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            speed_slider_, &QSlider::setValue);
    connect(speed_slider_, &QSlider::valueChanged, this, &PiperWidget::onSpeedChanged);

    connect(enable_btn_,  &QPushButton::clicked, this, &PiperWidget::onEnableClicked);
    connect(estop_btn_,   &QPushButton::clicked, this, &PiperWidget::onEmergencyStopClicked);
    connect(home_btn_,    &QPushButton::clicked, this, &PiperWidget::onHomeClicked);

    // joint tab
    for (int i = 0; i < 6; ++i) {
        connect(joint_sliders_[i], &QSlider::valueChanged, this, [this, i](int v) {
            joint_spins_[i]->blockSignals(true);
            joint_spins_[i]->setValue(double(v) / JOINT_SCALE);
            joint_spins_[i]->blockSignals(false);
        });
        connect(joint_sliders_[i], &QSlider::sliderReleased, this, [this, i]() {
            onJointSliderReleased(i);
        });
        connect(joint_spins_[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, i](double v) {
            joint_sliders_[i]->blockSignals(true);
            joint_sliders_[i]->setValue(int(v * JOINT_SCALE));
            joint_sliders_[i]->blockSignals(false);
            onJointSpinChanged(i, v);
        });
    }
    connect(send_joints_btn_, &QPushButton::clicked, this, &PiperWidget::onSendJointsClicked);
    connect(joints_zero_btn_, &QPushButton::clicked, this, &PiperWidget::onJointZeroClicked);

    // cartesian tab
    connect(send_cart_btn_, &QPushButton::clicked, this, &PiperWidget::onSendCartesianClicked);

    // gripper tab
    connect(gripper_slider_, &QSlider::valueChanged, this, [this](int v) {
        gripper_spin_->blockSignals(true);
        gripper_spin_->setValue(double(v) / 100.0);
        gripper_spin_->blockSignals(false);
    });
    connect(gripper_slider_, &QSlider::sliderReleased, this, &PiperWidget::onGripperSliderReleased);
    connect(gripper_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        gripper_slider_->blockSignals(true);
        gripper_slider_->setValue(int(v * 100));
        gripper_slider_->blockSignals(false);
    });
    connect(gripper_open_btn_,  &QPushButton::clicked, this, &PiperWidget::onGripperOpenClicked);
    connect(gripper_close_btn_, &QPushButton::clicked, this, &PiperWidget::onGripperCloseClicked);
}


// ════════════════════════════════════════════════════════════════════════
// Connection-state hooks
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::onRpcConnected() {
    conn_led_->setText("● 已连接");
    conn_led_->setStyleSheet("color:#3a8; font-weight:bold;");
    poll_state_timer_->start();
    poll_status_timer_->start();
    // Initial speed push
    onSpeedChanged(speed_slider_->value());
}

void PiperWidget::onRpcDisconnected() {
    conn_led_->setText("● 未连接");
    conn_led_->setStyleSheet("color:#c44; font-weight:bold;");
    poll_state_timer_->stop();
    poll_status_timer_->stop();
}


// ════════════════════════════════════════════════════════════════════════
// Periodic polls (RpcClient is async, so we fire-and-forget with callbacks)
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::pollJointsAndPose() {
    if (!rpc_) return;

    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
        [this](QJsonObject reply) {
            QJsonArray arr;
            if (reply.contains(Protocol::Fields::ANGLES))
                arr = reply.value(Protocol::Fields::ANGLES).toArray();
            else if (reply.contains("result"))
                arr = reply.value("result").toArray();        // raw bare array
            if (arr.size() == 6) {
                std::array<double, 6> j{};
                for (int i = 0; i < 6; ++i) j[i] = arr.at(i).toDouble();
                live_joints_deg_ = j;
                updateJointReadouts(j);
            }
        });

    rpc_->call(Protocol::Methods::ARM_GET_POSE, QJsonObject{},
        [this](QJsonObject reply) {
            // proc_piper returns arm.get_pose as List[float] (raw array).
            // RpcClient stashes that under "_array". Fallback to named
            // keys for any legacy / dict-shaped backend.
            QJsonArray arr = reply.value("_array").toArray();
            if (arr.size() != 6)
                arr = reply.value(Protocol::Fields::ANGLES).toArray();
            if (arr.size() == 6) {
                std::array<double, 6> p{};
                for (int i = 0; i < 6; ++i) p[i] = arr.at(i).toDouble();
                live_pose_ = p;
                updatePoseReadouts(p);
                // Mirror into the 3D viewer's HUD so X/Y/Z/RX/RY/RZ stay
                // in sync with the form-row readouts on the right panel.
                if (viewer_3d_) {
                    viewer_3d_->setEndEffectorPose(
                        float(p[0]), float(p[1]), float(p[2]),
                        float(p[3]), float(p[4]), float(p[5]));
                }
            }
        });
}

void PiperWidget::pollStatus() {
    if (!rpc_) return;
    rpc_->call(Protocol::Methods::PIPER_GET_STATUS, QJsonObject{},
        [this](QJsonObject reply) {
            const int ctrl   = reply.value("ctrl_mode").toInt(0);
            const int arms   = reply.value("arm_status").toInt(0);
            const int feed   = reply.value("mode_feed").toInt(0);
            const int motion = reply.value("motion_status").toInt(0);
            const int teach  = reply.value("teach_status").toInt(0);
            const bool hb    = reply.value("heartbeat_alive").toBool(false);
            ctrl_mode_       = ctrl;
            heartbeat_alive_ = hb;
            updateStatusBar(ctrl, arms, feed, motion, teach, hb);
        });
}


// ════════════════════════════════════════════════════════════════════════
// Top-bar action handlers
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::onEnableClicked() {
    // Re-run the V1.8-2 handshake. Useful if ctrl_mode somehow fell off CAN_CTRL.
    rpc_->call(Protocol::Methods::PIPER_HANDSHAKE, QJsonObject{}, nullptr);
}

void PiperWidget::onEmergencyStopClicked() {
    QJsonObject p; p["enable"] = true;
    rpc_->call(Protocol::Methods::ARM_EMERGENCY_STOP, p, nullptr);
}

void PiperWidget::onHomeClicked() {
    rpc_->call(Protocol::Methods::ARM_HOME, QJsonObject{}, nullptr);
}

void PiperWidget::onSpeedChanged(int pct) {
    // Map 1-100% to joint_dps via proc_piper's heuristic (100% ≈ 200 dps).
    QJsonObject p;
    p["joint_dps"] = double(pct) * 2.0;
    rpc_->call(Protocol::Methods::ARM_SET_SPEEDS, p, nullptr);
}


// ════════════════════════════════════════════════════════════════════════
// Joint tab handlers
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::onJointSliderReleased(int joint_idx) {
    const double deg = joint_spins_[joint_idx]->value();
    QJsonObject p;
    p["joint_index"] = joint_idx + 1;          // 1-based
    p["target_deg"]  = deg;
    rpc_->call(Protocol::Methods::ARM_MOVE_JOINT, p, nullptr);
}

void PiperWidget::onJointSpinChanged(int /*joint_idx*/, double /*deg*/) {
    // Don't auto-send on every spin change to avoid spamming;
    // user can either drag the slider (releases trigger send), use
    // the spin to set the value and click "发送目标".
}

void PiperWidget::onSendJointsClicked() {
    QJsonObject p;
    QJsonArray angles;
    for (int i = 0; i < 6; ++i) angles.append(joint_spins_[i]->value());
    p["angles"] = angles;
    rpc_->call(Protocol::Methods::ARM_MOVE_JOINTS, p, nullptr);
}

void PiperWidget::onJointZeroClicked() {
    for (int i = 0; i < 6; ++i) joint_spins_[i]->setValue(0.0);
}


// ════════════════════════════════════════════════════════════════════════
// Cartesian tab handlers
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::onJogClicked(int axis_idx, int direction) {
    const double step = cart_deltas_[axis_idx]->value() * direction;
    cart_spins_[axis_idx]->setValue(cart_spins_[axis_idx]->value() + step);
    onSendCartesianClicked();
}

void PiperWidget::onSendCartesianClicked() {
    QJsonObject p;
    p["x_mm"]      = cart_spins_[0]->value();
    p["y_mm"]      = cart_spins_[1]->value();
    p["z_mm"]      = cart_spins_[2]->value();
    p["roll_deg"]  = cart_spins_[3]->value();
    p["pitch_deg"] = cart_spins_[4]->value();
    p["yaw_deg"]   = cart_spins_[5]->value();

    const char *method = (cart_mode_idx_ == 1)
        ? Protocol::Methods::ARM_MOVE_LINEAR
        : Protocol::Methods::ARM_MOVE_POSE6D;
    rpc_->call(method, p, nullptr);
}

void PiperWidget::onCartesianModeChanged(int idx) {
    cart_mode_idx_ = idx;
}


// ════════════════════════════════════════════════════════════════════════
// Gripper tab handlers
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::onGripperSliderReleased() {
    QJsonObject p;
    p["angle_mm"] = gripper_spin_->value();
    rpc_->call(Protocol::Methods::PIPER_SET_GRIPPER_ANGLE, p, nullptr);
}

void PiperWidget::onGripperOpenClicked() {
    gripper_spin_->setValue(GRIPPER_MAX_MM);
    QJsonObject p;
    p["angle_mm"] = GRIPPER_MAX_MM;
    rpc_->call(Protocol::Methods::PIPER_SET_GRIPPER_ANGLE, p, nullptr);
}

void PiperWidget::onGripperCloseClicked() {
    gripper_spin_->setValue(GRIPPER_MIN_MM);
    QJsonObject p;
    p["angle_mm"] = GRIPPER_MIN_MM;
    rpc_->call(Protocol::Methods::PIPER_SET_GRIPPER_ANGLE, p, nullptr);
}


// ════════════════════════════════════════════════════════════════════════
// UI updaters
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::updateJointReadouts(const std::array<double, 6> &deg) {
    // The right-panel form-row labels were removed (now drawn as HUD on the
    // 3D viewer), but guard against any leftover pointer just in case.
    for (int i = 0; i < 6; ++i) {
        if (joint_readouts_[i])
            joint_readouts_[i]->setText(QString("%1°").arg(deg[i], 7, 'f', 2));
    }
    // Push live angles to ArmViewer3D — the viewer animates the chain
    // AND shows J1..J6 in its HUD using these same values.
    if (viewer_3d_) {
        QVector<float> v(6);
        for (int i = 0; i < 6; ++i) v[i] = float(deg[i]);
        viewer_3d_->setJointAngles(v);
    }
}

void PiperWidget::updatePoseReadouts(const std::array<double, 6> &p) {
    // pose_readout_ label was removed in favour of the 3D viewer's HUD
    // (setEndEffectorPose is called separately from pollJointsAndPose).
    if (pose_readout_) {
        pose_readout_->setText(QString("X=%1 Y=%2 Z=%3 mm  RX=%4 RY=%5 RZ=%6°")
                                  .arg(p[0], 7, 'f', 1)
                                  .arg(p[1], 7, 'f', 1)
                                  .arg(p[2], 7, 'f', 1)
                                  .arg(p[3], 6, 'f', 1)
                                  .arg(p[4], 6, 'f', 1)
                                  .arg(p[5], 6, 'f', 1));
    }
}

void PiperWidget::updateStatusBar(int ctrl_mode, int arm_status, int /*mode_feed*/,
                                   int motion_status, int /*teach_status*/, bool hb) {
    // The "控制" cell already shows TEACHING vs CAN_CTRL vs STANDBY based
    // on the firmware's reported ctrl_mode (see ctrlModeText). That's the
    // single source of truth for the operator — no software toggle here.
    status_ctrl_  ->setText(QString("控制: %1").arg(ctrlModeText(ctrl_mode)));
    status_arm_   ->setText(QString("状态: %1").arg(armStatusText(arm_status)));
    status_motion_->setText(QString("运动: %1").arg(motionStatusText(motion_status)));
    status_heartbeat_->setText(QString("心跳: %1").arg(hb ? "活" : "❌"));
    status_heartbeat_->setStyleSheet(hb ? "color:#3a8; font-family:monospace;"
                                        : "color:#c44; font-family:monospace;");

    // Visual recovery hint: when the firmware drops out of CAN_CTRL (撞机 /
    // 急停 / teach 退出后卡在 STANDBY), pulse the 使能/恢复 button red so
    // the operator's eye lands on it instead of hunting for what to click.
    if (enable_btn_) {
        const bool needs_recover = (ctrl_mode != 0x01);   // CAN_CTRL
        if (needs_recover) {
            enable_btn_->setStyleSheet(
                "QPushButton { background:#c44; color:white; font-weight:bold;"
                "              padding:4px 14px; border:2px solid #ffb000; }"
                "QPushButton:hover { background:#e55; }");
        } else {
            enable_btn_->setStyleSheet(
                "QPushButton { background:#3a8; color:white; font-weight:bold;"
                "              padding:4px 14px; }");
        }
    }
}

QString PiperWidget::ctrlModeText(int m) const {
    switch (m) {
        case 0x00: return "STANDBY";
        case 0x01: return "CAN_CTRL";
        case 0x02: return "TEACHING";
        case 0x03: return "ETH";
        case 0x04: return "WIFI";
        case 0x07: return "OFFLINE";
        default:   return QString("0x%1").arg(m, 2, 16, QChar('0'));
    }
}

QString PiperWidget::armStatusText(int s) const {
    switch (s) {
        case 0x00: return "NORMAL";
        case 0x01: return "E-STOP";
        case 0x02: return "TEACHING";
        case 0x05: return "JOINT_COMM_ERR";
        case 0x06: return "BRAKE_NOT_RELEASED";
        case 0x07: return "COLLISION";
        case 0x08: return "TEACH_OVERSPEED";
        case 0x09: return "JOINT_ABNORMAL";
        case 0x0A: return "OTHER";
        default:   return QString("0x%1").arg(s, 2, 16, QChar('0'));
    }
}

QString PiperWidget::motionStatusText(int s) const {
    switch (s) {
        case 0x00: return "已达目标";
        case 0x01: return "运动中";
        default:   return QString("0x%1").arg(s, 2, 16, QChar('0'));
    }
}


// ════════════════════════════════════════════════════════════════════════
// Persistence
// ════════════════════════════════════════════════════════════════════════
void PiperWidget::loadConfig(QSettings &s) {
    s.beginGroup("PiperWidget");
    if (s.contains("speed_pct")) {
        int p = s.value("speed_pct").toInt();
        speed_slider_->setValue(p);
        speed_spin_->setValue(p);
    }
    if (s.contains("cart_mode_idx")) cart_mode_idx_ = s.value("cart_mode_idx").toInt();
    s.endGroup();
}

void PiperWidget::saveConfig(QSettings &s) const {
    s.beginGroup("PiperWidget");
    s.setValue("speed_pct", speed_slider_->value());
    s.setValue("cart_mode_idx", cart_mode_idx_);
    s.endGroup();
}
