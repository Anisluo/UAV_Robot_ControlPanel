#include "Tab4TaskConfig.h"
#include "LogWidget.h"
#include "ArmViewer3D.h"
#include "ArmSyncWorker.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QFrame>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <QDir>
#include <QSlider>
#include <QVector3D>
#include <QGroupBox>
#include <QDateTime>
#include <QSignalBlocker>
#include <cmath>

Tab4TaskConfig::Tab4TaskConfig(RpcClient *rpc, QWidget *parent)
    : QWidget(parent)
    , rpc_(rpc)
{
    // Register POD / container types for queued signals across threads.
    qRegisterMetaType<ArmMeshCPU>("ArmMeshCPU");
    qRegisterMetaType<QVector<QVector3D>>("QVector<QVector3D>");
    qRegisterMetaType<QVector<float>>("QVector<float>");

    buildUi();
    start3DSimThread();
}

Tab4TaskConfig::~Tab4TaskConfig()
{
    stop3DSimThread();
}

void Tab4TaskConfig::buildUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(6, 6, 6, 6);
    outerLayout->setSpacing(6);

    // Layout: (3D viewer above sim panel) | task panel.  Both splitter
    // handles are draggable - user can resize viewer vs sliders on the
    // left side, and the whole 3D column vs the task column.
    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    auto *leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    leftSplitter->addWidget(build3DViewer());
    leftSplitter->addWidget(buildSimPanel());
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 1);
    leftSplitter->setSizes({520, 260});

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(buildTaskPanel());
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({760, 300});

    outerLayout->addWidget(mainSplitter, 3);

    // ── Bottom: log ───────────────────────────────────────────────────────
    log_widget_ = new LogWidget(rpc_, this);
    log_widget_->setMinimumHeight(120);
    outerLayout->addWidget(log_widget_, 1);
}

QWidget* Tab4TaskConfig::build3DViewer()
{
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(
        "QFrame { background-color: #0f1018; border: 1px solid #353650; border-radius: 4px; }");

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    viewer_3d_ = new ArmViewer3D(frame);
    viewer_3d_->setStatusText(QStringLiteral("3D Scene (drag to orbit, scroll to zoom)"));
    layout->addWidget(viewer_3d_, 1);

    return frame;
}

// (Old test bar lived here — replaced by buildSimPanel() on the right.)
#if 0
    // -- removed --
    // Lets the user verify the model rotates without the live arm. The
    // slider drives J1 directly (0..360°), and the sweep button triggers
    // an automatic sine animation. Both feed viewer.setJointAngles() —
    // exactly the same path the live RPC uses, so a working test bar
    // proves the viewer + transforms are correct and any "doesn't move
    // when arm moves" failure is upstream (RPC / network).
    auto *testBar = new QFrame(frame);
    testBar->setStyleSheet(
        "QFrame { background-color: #16182a; border-top: 1px solid #353650; }"
        "QLabel { color: #c8d0e8; }"
        "QPushButton { background: #283154; color: #ffffff; "
        "  border: 1px solid #4c5b88; border-radius: 3px; padding: 4px 10px; }"
        "QPushButton:checked { background: #ff8a3c; color: #1a1a2e; }"
        "QPushButton:hover { background: #364070; }");
    auto *barLayout = new QHBoxLayout(testBar);
    barLayout->setContentsMargins(8, 4, 8, 4);
    barLayout->setSpacing(8);

    barLayout->addWidget(new QLabel("测试 J1:", testBar));

    test_slider_j1_ = new QSlider(Qt::Horizontal, testBar);
    test_slider_j1_->setRange(0, 3600);   // tenths of a degree
    test_slider_j1_->setValue(0);
    test_slider_j1_->setMinimumWidth(180);
    barLayout->addWidget(test_slider_j1_, 1);

    test_angle_label_ = new QLabel("0.0°", testBar);
    test_angle_label_->setMinimumWidth(50);
    test_angle_label_->setStyleSheet("font-family: Consolas;");
    barLayout->addWidget(test_angle_label_);

    btn_test_sweep_ = new QPushButton("测试动画", testBar);
    btn_test_sweep_->setCheckable(true);
    btn_test_sweep_->setFixedHeight(24);
    barLayout->addWidget(btn_test_sweep_);

    layout->addWidget(testBar);

    // Slider drag -> push J1 to viewer.
    connect(test_slider_j1_, &QSlider::valueChanged, this,
            [this](int v) {
        const float deg = v / 10.0F;
        test_angle_label_->setText(QString::number(deg, 'f', 1) + "°");
        if (!viewer_3d_) return;
        QVector<float> a(6, 0.0F);
        a[0] = deg;
        viewer_3d_->setJointAngles(a);
    });

    // Sweep timer -- sin wave 0..360 at 0.5 Hz, 33 ms tick (~30 fps).
    test_sweep_timer_ = new QTimer(this);
    test_sweep_timer_->setInterval(33);
    connect(test_sweep_timer_, &QTimer::timeout, this, [this]() {
        test_sweep_phase_ += 0.033F * 0.5F * 2.0F * float(M_PI);  // 0.5 Hz
        const float deg = (1.0F + std::sin(test_sweep_phase_)) * 180.0F;
        test_slider_j1_->setValue(int(deg * 10.0F));   // updates label + viewer
    });

    // (test sweep button — removed)
#endif

// ── 3D sim thread ───────────────────────────────────────────────────────
// The worker lives on its own QThread and does the CPU-heavy STL parse
// on startup plus a lightweight angle-poll timer.  Rendering stays on
// the main thread because OpenGL contexts are thread-locked — what we
// offload is the mesh I/O and the polling cadence, not the draw calls.
void Tab4TaskConfig::start3DSimThread()
{
    if (sim_thread_) return;

    sim_thread_ = new QThread(this);
    sim_thread_->setObjectName("ArmSim");
    sim_worker_ = new ArmSyncWorker();      // no parent — owned by its thread
    sim_worker_->moveToThread(sim_thread_);

    // Cleanup when the thread finishes.
    connect(sim_thread_, &QThread::finished,
            sim_worker_, &QObject::deleteLater);

    // Worker → viewer (Qt::AutoConnection = QueuedConnection across threads).
    connect(sim_worker_, &ArmSyncWorker::configReady,
            this, [this](const QVector<QVector3D> &axes,
                          const QVector<QVector3D> &origins,
                          const QVector3D           &center,
                          float                      radius) {
        ArmViewer3D::Config cfg;
        cfg.joint_axes    = axes;
        cfg.joint_origins = origins;
        cfg.scene_center  = center;
        cfg.scene_radius  = radius;
        viewer_3d_->setConfig(cfg);
    });
    connect(sim_worker_, &ArmSyncWorker::meshReady,
            viewer_3d_,   &ArmViewer3D::addMeshCPU);
    connect(sim_worker_, &ArmSyncWorker::anglesReady,
            viewer_3d_,   &ArmViewer3D::setJointAngles);
    connect(sim_worker_, &ArmSyncWorker::loadComplete,
            this, [this](int n, QString status) {
        appendLog("INFO",
                  QStringLiteral("[3D] %1 (%2 个部件)")
                      .arg(status).arg(n));
    });
    connect(sim_worker_, &ArmSyncWorker::logMessage,
            this, [this](const QString &m) { appendLog("INFO", m); });

    // Main-thread bridge: the worker can't poke the RpcClient directly
    // (socket lives here); it just raises requestAngles() and we do the
    // call here, piping the reply back to the worker for fan-out.
    connect(sim_worker_, &ArmSyncWorker::requestAngles,
            this,         &Tab4TaskConfig::onAnglesRequested);

    sim_thread_->start();

    // Ask the worker to load assets. The model lives next to the
    // executable so a release build can ship without tools/.
    const QString exeDir = QCoreApplication::applicationDirPath();
    QDir d(exeDir);
    QString model_dir = d.absoluteFilePath("assets/arm_model");
    // Developer build: fall back to source tree if the deployed copy
    // isn't where we expected (e.g. first-run from build_win/).
    if (!QDir(model_dir).exists("arm_model.json")) {
        d.cdUp(); d.cdUp();   // build_win/ -> project root
        model_dir = d.absoluteFilePath("assets/arm_model");
    }
    QMetaObject::invokeMethod(sim_worker_, "loadAssets",
                              Qt::QueuedConnection,
                              Q_ARG(QString, model_dir));
    QMetaObject::invokeMethod(sim_worker_, "startAnglePolling",
                              Qt::QueuedConnection, Q_ARG(int, 200));
}

void Tab4TaskConfig::stop3DSimThread()
{
    if (!sim_thread_) return;
    if (sim_worker_) {
        QMetaObject::invokeMethod(sim_worker_, "stopAnglePolling",
                                  Qt::BlockingQueuedConnection);
    }
    sim_thread_->quit();
    sim_thread_->wait(2000);
    sim_thread_  = nullptr;
    sim_worker_  = nullptr;
}

void Tab4TaskConfig::onAnglesRequested()
{
    if (!rpc_ || !rpc_->isConnected() || !sim_worker_) return;
    // arm.get_angles is the gateway-side method that forwards to
    // proc_arm's arm.get_motor_angles (real encoder values) and unwraps
    // the bare array as {"angles":[j1..j6]} in degrees. A transient
    // error = silent skip — the next 200 ms tick retries.
    rpc_->call(QStringLiteral("arm.get_angles"), QJsonObject{},
        [this](QJsonObject reply) {
            if (!sim_worker_) return;
            const QJsonArray arr = reply.value("angles").toArray();
            QVector<float> angles;
            angles.reserve(arr.size());
            for (const auto &v : arr) angles.append(float(v.toDouble()));
            QMetaObject::invokeMethod(sim_worker_, "pushAngles",
                                       Qt::QueuedConnection,
                                       Q_ARG(QVector<float>, angles));
        });
}

// Sim panel + trajectory data + sim event handlers — split out only to
// keep this file readable.  Reads/writes the same Tab4TaskConfig members.
#include "Tab4SimPanel.inc"

QWidget* Tab4TaskConfig::buildTaskPanel()
{
    auto *grp = new QGroupBox("Task Configuration", this);
    auto *layout = new QVBoxLayout(grp);
    layout->setSpacing(8);
    layout->setContentsMargins(8, 18, 8, 8);

    layout->addWidget(new QLabel("可用任务列表:", grp));

    task_list_ = new QListWidget(grp);
    task_list_->setAlternatingRowColors(true);
    task_list_->setStyleSheet(
        "QListWidget { background: #ffffff; color: #1a1a2e; }"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:alternate { background: #f0f2f8; }"
        "QListWidget::item:selected { background: #1565c0; color: #ffffff; }"
        "QListWidget::item:hover:!selected { background: #dce4f5; }");

    // Pre-defined task entries
    auto addTask = [&](const QString &name, const QString &desc, const QString &key) {
        auto *item = new QListWidgetItem(
            QString("%1\n  %2").arg(name).arg(desc), task_list_);
        item->setData(Qt::UserRole, key);
        item->setSizeHint(QSize(0, 48));
    };

    addTask("机械臂演示运动", "单轴往复运动 (用于验证任务调度链路)",      "arm_demo");
    addTask("机械臂回零",     "六轴机械臂回零位",                          "arm_home");
    addTask("视觉抓取放置",   "相机识别 → 手眼标定 → 六轴抓取 → 放到目标位", "pick_place");
    addTask("电池拾取任务",   "识别方形电池 → 机械臂夹取 → 放入电池槽",    "battery_pick");
    addTask("平台锁定",       "锁定承载平台",                              "platform_lock");

    task_list_->setCurrentRow(0);
    layout->addWidget(task_list_, 1);

    // ── Normal action buttons ─────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btn_start_ = new QPushButton("执行选中任务", grp);
    btn_stop_  = new QPushButton("停止",         grp);
    btn_reset_ = new QPushButton("复位",         grp);
    btn_start_->setFixedHeight(30);
    btn_stop_->setFixedHeight(30);
    btn_reset_->setFixedHeight(30);
    btn_stop_->setEnabled(false);
    btnRow->addWidget(btn_start_);
    btnRow->addWidget(btn_stop_);
    btnRow->addWidget(btn_reset_);
    layout->addLayout(btnRow);

    // ── Big red E-STOP button ─────────────────────────────────────────────
    // Always enabled, deliberately styled to be impossible to miss.
    btn_estop_ = new QPushButton("急  停  ESTOP", grp);
    btn_estop_->setFixedHeight(56);
    btn_estop_->setStyleSheet(
        "QPushButton {"
        "  background: #c0392b;"
        "  color: white;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "  border: 3px solid #ffeb3b;"
        "  border-radius: 6px;"
        "  letter-spacing: 4px;"
        "}"
        "QPushButton:hover  { background: #e74c3c; }"
        "QPushButton:pressed{ background: #962d22; border-color: #fbc02d; }"
    );
    layout->addWidget(btn_estop_);

    // ── Status ────────────────────────────────────────────────────────────
    task_status_label_ = new QLabel("就绪", grp);
    task_status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    layout->addWidget(task_status_label_);

    connect(btn_start_, &QPushButton::clicked, this, &Tab4TaskConfig::onStartTask);
    connect(btn_stop_,  &QPushButton::clicked, this, &Tab4TaskConfig::onStopTask);
    connect(btn_reset_, &QPushButton::clicked, this, &Tab4TaskConfig::onResetTask);
    connect(btn_estop_, &QPushButton::clicked, this, &Tab4TaskConfig::onEstopTask);

    // Periodically poll the backend for task status so the GUI reflects
    // FSM transitions / failures even when no button is pressed.
    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this, &Tab4TaskConfig::onPollTaskStatus);
    poll_timer_->start(1000);

    return grp;
}

void Tab4TaskConfig::onStartTask()
{
    auto *cur = task_list_->currentItem();
    if (!cur) return;

    QString key = cur->data(Qt::UserRole).toString();
    QJsonObject params;
    params[Protocol::Fields::TASK_NAME] = key;

    rpc_->call(Protocol::Methods::TASK_START, params,
        [this](QJsonObject r) { onStartResult(r); });

    btn_start_->setEnabled(false);
    btn_stop_->setEnabled(true);
    task_status_label_->setText("任务执行中...");
    task_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
    log_widget_->appendLog("INFO", QString("[任务] 已发送启动指令: %1")
        .arg(task_list_->currentItem()->text().split('\n').first()));
}

void Tab4TaskConfig::onStopTask()
{
    // Immediately re-enable start so the user can restart without waiting for RPC reply
    btn_start_->setEnabled(true);
    btn_stop_->setEnabled(false);
    task_status_label_->setText("正在停止...");
    task_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");

    rpc_->call(Protocol::Methods::TASK_STOP, QJsonObject{},
        [this](QJsonObject r) { onStopResult(r); });
    log_widget_->appendLog("WARN", "[任务] 已发送停止指令");
}

void Tab4TaskConfig::onResetTask()
{
    rpc_->call(Protocol::Methods::TASK_RESET, QJsonObject{},
        [this](QJsonObject) {
            task_status_label_->setText("已复位");
            task_status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
            btn_start_->setEnabled(true);
            btn_stop_->setEnabled(false);
        });
    log_widget_->appendLog("INFO", "[任务] 已发送复位指令");
}

void Tab4TaskConfig::onEstopTask()
{
    // Hard estop: send TASK_STOP (which the gateway translates to "ESTOP")
    // unconditionally and immediately, no matter what state the GUI thinks
    // the task is in. The backend will:
    //  1. abort the running FSM
    //  2. call dev_emergency_stop_all() -> proc_arm.stop via unix socket
    //  3. transition into estop state
    btn_start_->setEnabled(false);
    btn_stop_->setEnabled(false);
    task_status_label_->setText("急停已触发，等待复位");
    task_status_label_->setStyleSheet(
        "font-family: Consolas; color: #ffeb3b; font-weight: bold;");
    log_widget_->appendLog("ERROR", "[任务] >>> 急停 <<<");

    if (rpc_ && rpc_->isConnected()) {
        rpc_->call(Protocol::Methods::TASK_STOP, QJsonObject{},
            [this](QJsonObject) {
                // Re-enable the start button after a short delay so the user
                // can rerun via 复位 -> 执行 without staring at a frozen UI.
                QTimer::singleShot(500, this, [this]() {
                    btn_start_->setEnabled(true);
                });
            });
    }
}

void Tab4TaskConfig::onPollTaskStatus()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::TASK_GET_STATUS, QJsonObject{},
        [this](QJsonObject result) {
            updateStatusFromBackend(result);
        });
}

void Tab4TaskConfig::updateStatusFromBackend(const QJsonObject &status)
{
    // Backend returns: {active, task, status, reason}
    const bool   active = status.value("active").toBool(false);
    const QString st    = status.value("status").toString("idle");
    const QString task  = status.value("task").toString("NONE");
    const QString reason = status.value("reason").toString();

    QString display;
    QString css = "font-family: Consolas; color: #dde1f0;";
    if (st == "running") {
        display = QString("运行中: %1").arg(task);
        css = "font-family: Consolas; color: #4caf50;";
        btn_start_->setEnabled(false);
        btn_stop_->setEnabled(true);
    } else if (st == "done") {
        display = QString("完成: %1").arg(task);
        css = "font-family: Consolas; color: #4caf50;";
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
    } else if (st == "failed") {
        display = QString("失败: %1").arg(reason);
        css = "font-family: Consolas; color: #f44336;";
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
    } else if (st == "stopped") {
        display = QString("已停止: %1").arg(reason);
        css = "font-family: Consolas; color: #ffeb3b;";
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
    } else {
        display = "就绪";
        if (!active) {
            btn_start_->setEnabled(true);
            btn_stop_->setEnabled(false);
        }
    }
    task_status_label_->setText(display);
    task_status_label_->setStyleSheet(css);
}

void Tab4TaskConfig::onStartResult(QJsonObject result)
{
    bool ok = result.value("ok").toBool(false);
    if (ok) {
        task_status_label_->setText("任务运行中");
        task_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
        log_widget_->appendLog("INFO", "[任务] 启动成功");
    } else {
        task_status_label_->setText("启动失败: " + result.value("error").toString());
        task_status_label_->setStyleSheet("font-family: Consolas; color: #f44336;");
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
        log_widget_->appendLog("ERROR", "[任务] 启动失败");
    }
}

void Tab4TaskConfig::onStopResult(QJsonObject result)
{
    Q_UNUSED(result)
    btn_start_->setEnabled(true);
    btn_stop_->setEnabled(false);
    task_status_label_->setText("任务已停止");
    task_status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    log_widget_->appendLog("INFO", "[任务] 已停止");
}

void Tab4TaskConfig::appendLog(const QString &level, const QString &msg)
{
    log_widget_->appendLog(level, msg);
}

void Tab4TaskConfig::setConnectionParams(const QString &host, quint16 rpc_port, quint16 vid_port)
{
    Q_UNUSED(host) Q_UNUSED(rpc_port) Q_UNUSED(vid_port)
    // Reserved for future per-tab connection display
}
