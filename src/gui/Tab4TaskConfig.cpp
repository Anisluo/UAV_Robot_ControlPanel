#include "Tab4TaskConfig.h"
#include "LogWidget.h"
#include "ArmViewer3D.h"
#include "ArmSyncWorker.h"
#include "TaskFlowWidget.h"
#include "TaskStep.h"
#include "StageConfigDialog.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QRadioButton>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

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

    // Create the log widget FIRST so buildTaskPanel()'s startup-log calls
    // (e.g. "loaded N saved scripts") have a live target. Adding it to the
    // outerLayout last is fine — Qt layouts are independent of construction
    // order.
    log_widget_ = new LogWidget(rpc_, this);
    log_widget_->setMinimumHeight(70);
    log_widget_->setMaximumHeight(110);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    auto *leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    leftSplitter->addWidget(build3DViewer());
    leftSplitter->addWidget(buildCameraDock());
    leftSplitter->setStretchFactor(0, 3);
    leftSplitter->setStretchFactor(1, 1);
    leftSplitter->setSizes({520, 260});

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(buildTaskPanel());
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 5);
    mainSplitter->setSizes({420, 1000});

    outerLayout->addWidget(mainSplitter, 6);
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
    // Stop polling via a NON-blocking queued call. A BlockingQueuedConnection
    // here would deadlock at shutdown if the worker is mid-loadAssets (a long
    // synchronous slot) — the worker can't drain its event queue until
    // loadAssets returns, and the GUI thread would be stuck waiting on it.
    if (sim_worker_) {
        QMetaObject::invokeMethod(sim_worker_, "stopAnglePolling",
                                  Qt::QueuedConnection);
    }
    // Also drop the pending-RPC bridge so a late reply doesn't fire into a
    // half-destroyed widget.
    if (rpc_) disconnect(rpc_, nullptr, this, nullptr);

    sim_thread_->quit();
    // Generous wait — loadAssets parses 10 STL files which can run a couple
    // of seconds on first start; we'd rather block briefly than orphan the
    // thread (which would leak the worker and confuse the next QApplication
    // exit).
    if (!sim_thread_->wait(5000)) {
        // Worst case: the worker is wedged on file I/O. Force-terminate so
        // the process can actually exit. May leak the worker QObject, but
        // we're already on the way out.
        sim_thread_->terminate();
        sim_thread_->wait(500);
    }
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

// ────────────────────────────────────────────────────────────────────────
// Camera dock — empty container that the MainWindow drops the shared
// CameraWidget into when the user is on this tab. Same outer footprint
// as the legacy 3D-sim panel so the splitter geometry doesn't shift.
// ────────────────────────────────────────────────────────────────────────
QWidget* Tab4TaskConfig::buildCameraDock()
{
    auto *grp = new QGroupBox(QStringLiteral("相机视频"), this);
    auto *layout = new QVBoxLayout(grp);
    layout->setContentsMargins(8, 18, 8, 8);
    layout->setSpacing(0);

    cam_dock_ = new QWidget(grp);
    auto *dl = new QVBoxLayout(cam_dock_);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(0);
    cam_dock_->setMinimumHeight(160);
    layout->addWidget(cam_dock_, 1);

    return grp;
}

void Tab4TaskConfig::mountCamera(QWidget *cam)
{
    if (!cam_dock_ || !cam) return;
    if (cam->parentWidget() == cam_dock_) return;
    cam->setParent(cam_dock_);
    cam_dock_->layout()->addWidget(cam);
    cam->show();
}

void Tab4TaskConfig::unmountCamera()
{
    // The MainWindow re-parents the camera onto its own splitter; the
    // QLayout drops the widget reference automatically when setParent()
    // is called with a different owner, so this is a no-op hook kept for
    // symmetry / future cleanup.
}

// Sim panel + trajectory data + sim event handlers — split out only to
// keep this file readable.  Reads/writes the same Tab4TaskConfig members.
// buildSimPanel() is no longer wired into the UI (the camera dock took
// its slot), but the trajectory/animation helpers remain compiled for
// potential reuse from menu actions.
#include "Tab4SimPanel.inc"

QWidget* Tab4TaskConfig::buildTaskPanel()
{
    auto *grp = new QGroupBox("电池换装任务", this);
    auto *layout = new QVBoxLayout(grp);
    layout->setSpacing(6);
    layout->setContentsMargins(8, 18, 8, 8);

    // ── Top toolbar: mode + run buttons + status ─────────────────────────
    auto *bar = new QHBoxLayout;

    auto *mode_box = new QButtonGroup(grp);
    mode_sim_radio_  = new QRadioButton("模拟", grp);
    mode_real_radio_ = new QRadioButton("实机", grp);
    mode_step_radio_ = new QRadioButton("单步", grp);
    mode_sim_radio_->setChecked(true);
    mode_box->addButton(mode_sim_radio_,  0);
    mode_box->addButton(mode_real_radio_, 1);
    mode_box->addButton(mode_step_radio_, 2);
    bar->addWidget(new QLabel("模式:", grp));
    bar->addWidget(mode_sim_radio_);
    bar->addWidget(mode_real_radio_);
    bar->addWidget(mode_step_radio_);
    bar->addSpacing(20);

    btn_flow_start_ = new QPushButton("▶ 开始", grp);
    btn_flow_stop_  = new QPushButton("⏸ 停止", grp);
    btn_flow_reset_ = new QPushButton("↻ 复位", grp);
    btn_estop_      = new QPushButton("急停 ESTOP", grp);
    btn_flow_start_->setFixedHeight(32);
    btn_flow_stop_->setFixedHeight(32);
    btn_flow_reset_->setFixedHeight(32);
    // Estop sits next to the reset button on the same bar — slightly taller
    // and wider than its neighbours so it still reads as the panic action
    // without dominating the layout.
    btn_estop_->setFixedHeight(36);
    btn_estop_->setMinimumWidth(120);
    btn_flow_stop_->setEnabled(false);
    btn_flow_start_->setStyleSheet(
        "QPushButton { background:#3a8; color:white; font-weight:bold; padding:4px 14px; }"
        "QPushButton:disabled { background:#446; color:#aab; }");
    btn_flow_stop_->setStyleSheet(
        "QPushButton { background:#c33; color:white; font-weight:bold; padding:4px 14px; }"
        "QPushButton:disabled { background:#553; color:#aab; }");
    btn_estop_->setStyleSheet(
        "QPushButton {"
        "  background:#c0392b; color:white; font-weight:bold;"
        "  border:2px solid #ffeb3b; border-radius:5px;"
        "  padding:2px 14px; letter-spacing:1px;"
        "}"
        "QPushButton:hover  { background:#e74c3c; }"
        "QPushButton:pressed{ background:#962d22; border-color:#fbc02d; }");
    bar->addWidget(btn_flow_start_);
    bar->addWidget(btn_flow_stop_);
    bar->addWidget(btn_flow_reset_);
    bar->addSpacing(10);

    btn_flow_export_ = new QPushButton(QStringLiteral("💾 导出"), grp);
    btn_flow_import_ = new QPushButton(QStringLiteral("📂 加载"), grp);
    btn_flow_export_->setToolTip(QStringLiteral("把当前 9 个 stage 的录制脚本导出到 JSON 文件"));
    btn_flow_import_->setToolTip(QStringLiteral("从 JSON 文件加载任务脚本, 覆盖当前所有 stage"));
    btn_flow_export_->setFixedHeight(32);
    btn_flow_import_->setFixedHeight(32);
    btn_flow_export_->setStyleSheet("QPushButton{ background:#445; color:#ddd; padding:4px 10px; border-radius:4px; }"
                                     "QPushButton:hover{ background:#556; }");
    btn_flow_import_->setStyleSheet("QPushButton{ background:#445; color:#ddd; padding:4px 10px; border-radius:4px; }"
                                     "QPushButton:hover{ background:#556; }");
    bar->addWidget(btn_flow_export_);
    bar->addWidget(btn_flow_import_);
    bar->addSpacing(10);

    // Phase-only shortcuts: 取电 (phase1 = stage 1..5) / 换电 (phase2 = stage 6..9).
    // Both use the implementation that ▶ 开始 / 实机模式 uses internally — only
    // difference is they cap flow_real_end_stage_idx_ so the run stops at the
    // phase boundary instead of marching through all 9 stages.
    btn_flow_pickup_ = new QPushButton(QStringLiteral("🔋 取电"), grp);
    btn_flow_swap_   = new QPushButton(QStringLiteral("🔁 换电"), grp);
    btn_flow_pickup_->setToolTip(QStringLiteral("跑 phase1: stage 1~5 (INIT → PLATFORM). 需 RPC 已连接 + 已录脚本"));
    btn_flow_swap_  ->setToolTip(QStringLiteral("跑 phase2: stage 6~9 (FETCH → DONE). 需 RPC 已连接 + 已录脚本"));
    btn_flow_pickup_->setFixedHeight(32);
    btn_flow_swap_  ->setFixedHeight(32);
    btn_flow_pickup_->setStyleSheet(
        "QPushButton{ background:#6a4; color:white; font-weight:bold; padding:4px 12px; border-radius:4px; }"
        "QPushButton:hover{ background:#7c5; } QPushButton:disabled{ background:#446; color:#aab; }");
    btn_flow_swap_  ->setStyleSheet(
        "QPushButton{ background:#a64; color:white; font-weight:bold; padding:4px 12px; border-radius:4px; }"
        "QPushButton:hover{ background:#b75; } QPushButton:disabled{ background:#446; color:#aab; }");
    bar->addWidget(btn_flow_pickup_);
    bar->addWidget(btn_flow_swap_);
    bar->addSpacing(10);

    bar->addWidget(btn_estop_);
    bar->addStretch(1);

    flow_status_label_ = new QLabel("就绪 · 模式: 模拟", grp);
    flow_status_label_->setStyleSheet("font-family: Consolas; color: #c0d0f0;");
    bar->addWidget(flow_status_label_);

    layout->addLayout(bar);

    // ── Flow chart — the main attraction ─────────────────────────────────
    flow_widget_ = new TaskFlowWidget(grp);
    layout->addWidget(flow_widget_, /*stretch=*/1);

    // ── Legacy single-task pieces (kept invisible by default so the rest
    //    of the file's old onStartTask/onStopTask plumbing still compiles
    //    and can be wired up if you re-enable a tiny task panel later.) ──
    task_list_         = new QListWidget(grp);
    btn_start_         = new QPushButton("legacy_start", grp);
    btn_stop_          = new QPushButton("legacy_stop",  grp);
    btn_reset_         = new QPushButton("legacy_reset", grp);
    task_status_label_ = new QLabel("", grp);
    task_list_->hide(); btn_start_->hide(); btn_stop_->hide(); btn_reset_->hide();
    task_status_label_->hide();

    // ── Signal wiring ────────────────────────────────────────────────────
    connect(mode_sim_radio_,  &QRadioButton::toggled, this, &Tab4TaskConfig::onFlowModeChanged);
    connect(mode_real_radio_, &QRadioButton::toggled, this, &Tab4TaskConfig::onFlowModeChanged);
    connect(mode_step_radio_, &QRadioButton::toggled, this, &Tab4TaskConfig::onFlowModeChanged);
    connect(btn_flow_start_, &QPushButton::clicked,   this, &Tab4TaskConfig::onFlowStart);
    connect(btn_flow_stop_,  &QPushButton::clicked,   this, &Tab4TaskConfig::onFlowStop);
    connect(btn_flow_reset_, &QPushButton::clicked,   this, &Tab4TaskConfig::onFlowReset);
    connect(btn_flow_export_,&QPushButton::clicked,   this, &Tab4TaskConfig::onFlowExport);
    connect(btn_flow_import_,&QPushButton::clicked,   this, &Tab4TaskConfig::onFlowImport);
    connect(btn_flow_pickup_,&QPushButton::clicked,   this, &Tab4TaskConfig::onFlowPickup);
    connect(btn_flow_swap_,  &QPushButton::clicked,   this, &Tab4TaskConfig::onFlowSwap);
    connect(btn_estop_,      &QPushButton::clicked,   this, &Tab4TaskConfig::onEstopTask);
    connect(flow_widget_,    &TaskFlowWidget::stationClicked,
            this,            &Tab4TaskConfig::onFlowStationClicked);
    connect(flow_widget_,    &TaskFlowWidget::stageConfigClicked,
            this,            &Tab4TaskConfig::onStageConfigClicked);

    // Load any previously-saved per-stage TaskStep scripts from the
    // operator's user-config directory. This is the persistent script
    // store that StageConfigDialog reads/writes.
    {
        TaskConfig cfg = TaskConfig::loadFromHomeFile();
        stage_scripts_ = cfg.scripts;
        int total = 0;
        for (const auto &v : stage_scripts_) total += v.size();
        if (total > 0) {
            appendLog("info", QString("[stages] loaded %1 step(s) across %2 stage(s) from %3")
                                  .arg(total)
                                  .arg(stage_scripts_.size())
                                  .arg(TaskConfig::homeFilePath()));
        }
    }

    // ── Timers ──────────────────────────────────────────────────────────
    flow_sim_timer_ = new QTimer(this);
    flow_sim_timer_->setInterval(33);       // ~30 Hz interpolation
    connect(flow_sim_timer_, &QTimer::timeout, this, &Tab4TaskConfig::onFlowSimTick);

    swap_poll_timer_ = new QTimer(this);
    swap_poll_timer_->setInterval(500);     // 2 Hz polling
    connect(swap_poll_timer_, &QTimer::timeout, this, &Tab4TaskConfig::onSwapStatusPoll);

    poll_timer_ = new QTimer(this);   // kept for the (hidden) legacy task panel
    poll_timer_->setSingleShot(true); // never fires unless explicitly started

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
    if (log_widget_) log_widget_->appendLog(level, msg);
}

void Tab4TaskConfig::setConnectionParams(const QString &host, quint16 rpc_port, quint16 vid_port)
{
    Q_UNUSED(host) Q_UNUSED(rpc_port) Q_UNUSED(vid_port)
    // Reserved for future per-tab connection display
}


// ════════════════════════════════════════════════════════════════════════
// Battery-swap flow pipeline (TaskFlowWidget integration)
// ════════════════════════════════════════════════════════════════════════
void Tab4TaskConfig::onFlowModeChanged()
{
    if (flow_running_) {
        // Don't allow mode change mid-flight — operator likely meant to stop first.
        appendLog("warn", "正在运行中, 请先停止再切换模式");
        QSignalBlocker b1(mode_sim_radio_);
        QSignalBlocker b2(mode_real_radio_);
        QSignalBlocker b3(mode_step_radio_);
        mode_sim_radio_->setChecked(flow_simulating_);
        mode_real_radio_->setChecked(!flow_simulating_);
        mode_step_radio_->setChecked(false);
        return;
    }
    const bool step = mode_step_radio_->isChecked();
    if (flow_widget_) flow_widget_->setClickEnabled(step);   // ← 只在单步模式接收点击
    if (step) {
        // 单步调试模式: 复用 开始/停止 按钮做"执行选中"/"跳过"
        btn_flow_start_->setText("▶ 执行选中");
        btn_flow_stop_->setText("⏭ 跳过(标完成)");
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setEnabled(true);
        flow_status_label_->setText(
            flow_step_selected_stage_.isEmpty()
                ? "单步调试 · 在流程图中点击一张卡片选取步骤"
                : QString("单步调试 · 已选: %1 · 点 ▶ 全部执行").arg(flow_step_selected_stage_));
    } else {
        btn_flow_start_->setText("▶ 开始");
        btn_flow_stop_->setText("⏸ 停止");
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setEnabled(false);
        const bool sim = mode_sim_radio_->isChecked();
        flow_status_label_->setText(QString("就绪 · 模式: %1").arg(sim ? "模拟" : "实机"));
        // 切出单步时清掉选中高亮 + 终止可能在跑的子状态序列
        if (step_advance_timer_) step_advance_timer_->stop();
        flow_step_run_idx_ = -1;
        flow_step_selected_stage_.clear();
        flow_step_states_to_run_.clear();
        if (flow_widget_) flow_widget_->setSelectedStage(QString());
    }
}

// Poll airport.get_status until every rail in `watched_rails` reports
// STALLED (state == 2). When that happens, stop the step-advance timer
// and trigger the next step immediately — saves the operator from
// waiting out the max_ms upper bound.
//
// Re-arms itself with a 200 ms singleShot until: (a) all watched rails
// stalled → manual advance; or (b) flow_step_run_idx_ changed → stale,
// drop silently; or (c) max_ms timeout fired the timer anyway → also
// stale-drop.
void Tab4TaskConfig::pollAirportRailDone(const QVector<int> &watched_rails,
                                          const QString &mode,
                                          const QString &label,
                                          int session_run_idx)
{
    if (flow_step_run_idx_ != session_run_idx) return;
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::AIRPORT_GET_STATUS, QJsonObject{},
        [this, watched_rails, mode, label, session_run_idx](QJsonObject reply) {
            if (flow_step_run_idx_ != session_run_idx) return;
            const QJsonArray rails = reply.value("rails").toArray();
            int state[3] = {0, 0, 0};
            for (const QJsonValue &v : rails) {
                const QJsonObject o = v.toObject();
                const int idx = o.value("index").toInt(-1);
                if (idx >= 0 && idx < 3) state[idx] = o.value("state").toInt(0);
            }
            // Wait condition depends on the step's stop_mode:
            //   stall    → state == 2 (STALLED)
            //   distance → state != 1 (anything except MOVING; IDLE means
            //              reached target, STALLED means hit obstacle
            //              early — both are valid completion).
            bool all_done = !watched_rails.isEmpty();
            for (int r : watched_rails) {
                if (r < 0 || r >= 3) { all_done = false; break; }
                if (mode == "distance") {
                    if (state[r] == 1) { all_done = false; break; }
                } else {
                    if (state[r] != 2) { all_done = false; break; }
                }
            }
            if (all_done) {
                appendLog("info",
                    QString("✓ %1 完成, 提前推进").arg(label));
                if (step_advance_timer_ && step_advance_timer_->isActive()) {
                    step_advance_timer_->stop();
                }
                onFlowStepAdvance();
                return;
            }
            QTimer::singleShot(200, this,
                [this, watched_rails, mode, label, session_run_idx]() {
                    pollAirportRailDone(watched_rails, mode, label, session_run_idx);
                });
        });
}

void Tab4TaskConfig::pollDoorAxisDone(const QString &axis_key,
                                       const QString &label,
                                       int session_run_idx)
{
    if (flow_step_run_idx_ != session_run_idx) return;
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call(Protocol::Methods::DOOR_GET_STATUS, QJsonObject{},
        [this, axis_key, label, session_run_idx](QJsonObject reply) {
            if (flow_step_run_idx_ != session_run_idx) return;

            // proc_door unreachable / module offline: don't spin forever —
            // let the step's max_ms timer carry the flow forward.
            if (!reply.value("connected").toBool(false)) {
                appendLog("warn",
                    QString("⚠ %1: 继电器模块无响应, 按最长等待推进").arg(label));
                return;
            }

            const QJsonObject axis = reply.value(axis_key).toObject();
            if (!axis.value("moving").toBool(false)) {
                // Not moving any more. proc_door already de-energised the
                // coils; reason tells us whether it arrived or gave up.
                const QString reason = axis.value("reason").toString();
                const QString state  = axis.value("state").toString();
                if (reason == "timeout") {
                    appendLog("warn",
                        QString("⚠ %1 超时未到位 (state=%2), 后端已断电").arg(label).arg(state));
                } else {
                    appendLog("info",
                        QString("✓ %1 完成 (state=%2), 提前推进").arg(label).arg(state));
                }
                if (step_advance_timer_ && step_advance_timer_->isActive()) {
                    step_advance_timer_->stop();
                }
                onFlowStepAdvance();
                return;
            }
            QTimer::singleShot(200, this,
                [this, axis_key, label, session_run_idx]() {
                    pollDoorAxisDone(axis_key, label, session_run_idx);
                });
        });
}

// Build the sim playlist: walk each stage in pipeline order; for each
// stage, expand into either (a) the configured TaskStep script's steps
// if one was recorded, or (b) the stage's fine-grained demo states as
// fallback. Each entry carries its own joint target + duration so the
// simulator just walks the list without per-tick conditional logic.
QVector<Tab4TaskConfig::SimSegment> Tab4TaskConfig::buildSimPlaylist() const
{
    QVector<SimSegment> out;
    const auto &stages = TaskFlowWidget::stages();
    const auto &states = TaskFlowWidget::states();

    // Prior pose (initial joints = 0). Used to size MOVE_JOINTS segments
    // by joint travel — large moves get longer animations.
    QVector<float> prev_joints(6, 0.0f);

    auto segDurFromTravel = [](const QVector<float> &from,
                               const QVector<float> &to,
                               double speed_ratio) -> int {
        if (from.size() != 6 || to.size() != 6) return 1500;
        float max_delta = 0.0f;
        for (int i = 0; i < 6; ++i) {
            max_delta = std::max(max_delta, std::abs(to[i] - from[i]));
        }
        // 60 deg/s at speed_ratio=1; scale down at lower ratios.
        const double sr = (speed_ratio > 0.05) ? speed_ratio : 0.30;
        const double deg_per_s = 60.0 * sr;
        const int travel_ms = int(max_delta * 1000.0 / std::max(1.0, deg_per_s));
        return std::max(600, travel_ms + 300);
    };

    for (const TaskStage &stage : stages) {
        const auto sit = stage_scripts_.constFind(stage.id);
        const bool has_script = (sit != stage_scripts_.constEnd() && !sit.value().isEmpty());

        if (has_script) {
            const QVector<TaskStep> &script = sit.value();
            // Find the first fine-state of this stage so flow_widget_
            // shows the right card as "active" while the script runs.
            QString first_state_id;
            const QVector<QString> stage_states = TaskFlowWidget::statesInStage(stage.id);
            if (!stage_states.isEmpty()) first_state_id = stage_states.first();

            bool first_seg_of_stage = true;
            for (const TaskStep &step : script) {
                SimSegment seg;
                seg.stage_id       = stage.id;
                seg.is_script_step = true;
                if (first_seg_of_stage) {
                    seg.state_id = first_state_id;
                    first_seg_of_stage = false;
                }

                switch (step.type) {
                    case StepType::MOVE_JOINTS: {
                        const QVariantList j = step.params.value("joints").toList();
                        if (j.size() == 6) {
                            seg.target_joints.resize(6);
                            for (int i = 0; i < 6; ++i)
                                seg.target_joints[i] = float(j[i].toDouble());
                            const double sr = step.params.value("speed_ratio", 0.30).toDouble();
                            seg.duration_ms = segDurFromTravel(prev_joints, seg.target_joints, sr);
                            prev_joints = seg.target_joints;
                            seg.label = QStringLiteral("[%1] %2  %3")
                                            .arg(stage.title).arg("MOVE_JOINTS")
                                            .arg(step.label.isEmpty() ? step.summary() : step.label);
                        } else {
                            seg.duration_ms = 600;
                            seg.label = QStringLiteral("[%1] MOVE_JOINTS (无效)").arg(stage.title);
                        }
                        break;
                    }
                    case StepType::GRIPPER:
                        seg.duration_ms = 800;
                        seg.label = QStringLiteral("[%1] GRIPPER %2").arg(stage.title).arg(step.summary());
                        break;
                    case StepType::AIRPORT_RAIL:
                    case StepType::AIRPORT_GRIPPER:
                        // Stall-driven on real hardware; in sim just dwell briefly.
                        seg.duration_ms = 1500;
                        seg.label = QStringLiteral("[%1] %2 %3")
                                        .arg(stage.title)
                                        .arg(TaskStep::typeLabel(step.type))
                                        .arg(step.summary());
                        break;
                    case StepType::WAIT_DETECT_UAV:
                    case StepType::WAIT_DETECT_BAT:
                        // Sim can't actually poll detection; show brief.
                        seg.duration_ms = 800;
                        seg.label = QStringLiteral("[%1] %2 (sim 跳过等待)")
                                        .arg(stage.title)
                                        .arg(TaskStep::typeLabel(step.type));
                        break;
                    case StepType::DWELL:
                        seg.duration_ms = std::max(200, step.params.value("ms", 1000).toInt());
                        seg.label = QStringLiteral("[%1] DWELL %2ms").arg(stage.title).arg(seg.duration_ms);
                        break;
                    case StepType::MOVE_CARTESIAN:
                        // No FK to play this faithfully in viewer; brief hold.
                        seg.duration_ms = 1500;
                        seg.label = QStringLiteral("[%1] CARTESIAN (sim 占位)").arg(stage.title);
                        break;
                    case StepType::FIX_POINT:
                        // Sim 没 FK, 显示成 dwell.
                        seg.duration_ms = std::max(500,
                            step.params.value("duration_ms", 5000).toInt());
                        seg.label = QStringLiteral("[%1] 定点跟踪 %2ms")
                                        .arg(stage.title).arg(seg.duration_ms);
                        break;
                    case StepType::DOOR:
                    case StepType::HELIPAD:
                        // No 3-D model for the hatch / lift, so the sim just
                        // dwells for a plausible travel time.
                        seg.duration_ms =
                            (step.params.value("action").toString() == "stop") ? 300 : 2000;
                        seg.label = QStringLiteral("[%1] %2 %3")
                                        .arg(stage.title)
                                        .arg(TaskStep::typeLabel(step.type))
                                        .arg(step.summary());
                        break;
                }
                out.append(std::move(seg));
            }
        } else {
            // No script for this stage — fall back to the legacy fine-state
            // demo walk: one segment per fine-state.
            for (const QString &sid : TaskFlowWidget::statesInStage(stage.id)) {
                int idx = -1;
                for (int i = 0; i < states.size(); ++i) {
                    if (states[i].id == sid) { idx = i; break; }
                }
                if (idx < 0) continue;
                SimSegment seg;
                seg.stage_id      = stage.id;
                seg.state_id      = sid;
                seg.is_script_step = false;
                if (states[idx].demo_joints_deg.size() == 6) {
                    seg.target_joints = states[idx].demo_joints_deg;
                    prev_joints = seg.target_joints;
                }
                seg.duration_ms = 1500;
                seg.label = QStringLiteral("[%1] %2 (demo)")
                                .arg(stage.title).arg(states[idx].label);
                out.append(std::move(seg));
            }
        }
    }
    return out;
}

void Tab4TaskConfig::onFlowStart()
{
    // 单步模式: 复用"开始"按钮做"执行选中步" - 不走全流程逻辑
    if (mode_step_radio_ && mode_step_radio_->isChecked()) {
        onFlowStepExecute();
        return;
    }

    if (flow_running_) return;
    flow_widget_->resetAll();

    flow_simulating_ = mode_sim_radio_->isChecked();
    flow_running_    = true;
    btn_flow_start_->setEnabled(false);
    btn_flow_stop_->setEnabled(true);

    if (flow_simulating_) {
        appendLog("info", "模拟模式启动 — 流程图驱动 3D 视图, 不接触真实机械臂");
        if (sim_worker_) {
            QMetaObject::invokeMethod(sim_worker_, "stopAnglePolling",
                                      Qt::QueuedConnection);
        }

        // Build the playlist from configured scripts (preferred) + demo
        // fine-state poses (fallback). This is what the user means by "sim
        // should use the points I configured" — each MOVE_JOINTS step in
        // a stage's script becomes one animation segment.
        sim_playlist_ = buildSimPlaylist();
        if (sim_playlist_.isEmpty()) {
            appendLog("warn", "模拟模式: 无可播放段 (流程图为空且无脚本)");
            flow_running_ = false;
            flow_simulating_ = false;
            btn_flow_start_->setEnabled(true);
            btn_flow_stop_->setEnabled(false);
            return;
        }
        int scripted_segs = 0;
        for (const auto &seg : sim_playlist_) if (seg.is_script_step) ++scripted_segs;
        appendLog("info",
            QString("模拟段共 %1 (其中脚本步 %2, 默认 demo 段 %3)")
                .arg(sim_playlist_.size())
                .arg(scripted_segs)
                .arg(sim_playlist_.size() - scripted_segs));

        flow_sim_start_joints_   = { 0, 0, 0, 0, 0, 0 };
        sim_seg_idx_             = 0;
        const SimSegment &first  = sim_playlist_[0];
        flow_sim_step_dur_ms_    = first.duration_ms;
        flow_sim_step_start_ms_  = QDateTime::currentMSecsSinceEpoch();
        if (!first.state_id.isEmpty()) flow_widget_->setCurrentState(first.state_id);
        flow_status_label_->setText(QString("模拟运行 · 1/%1 · %2")
                                       .arg(sim_playlist_.size())
                                       .arg(first.label));
        flow_sim_timer_->start();
    } else {
        // 实机模式 = 把 9 个 stage 的录制脚本一个接一个串起来跑, 用单步那套
        // dispatchScriptStep + step_advance_timer 机制 (跟单步唯一区别是
        // 当前 stage 跑完不停, 自动跳到下一个 stage).
        if (!rpc_ || !rpc_->isConnected()) {
            appendLog("error", "实机模式: RPC 未连接");
            flow_running_ = false;
            btn_flow_start_->setEnabled(true);
            btn_flow_stop_->setEnabled(false);
            return;
        }
        appendLog("info", "实机模式启动 — 串行执行 9 个 stage 的录制脚本");
        flow_real_sequential_     = true;
        flow_real_stage_idx_      = -1;   // startRealStage 会从 0 开始找
        flow_real_end_stage_idx_  = -1;   // ▶ 开始走老路径 — 跑到底

        // step_advance_timer 没建过先建
        if (!step_advance_timer_) {
            step_advance_timer_ = new QTimer(this);
            step_advance_timer_->setInterval(1500);
            connect(step_advance_timer_, &QTimer::timeout,
                    this, &Tab4TaskConfig::onFlowStepAdvance);
        }

        // 从 stage 0 开始 — startRealStage 会跳过没录脚本的 stage
        if (!startRealStage(0)) {
            appendLog("warn", "实机模式: 9 个 stage 全部没有录制脚本, 无事可做");
            flow_real_sequential_ = false;
            flow_running_ = false;
            btn_flow_start_->setEnabled(true);
            btn_flow_stop_->setEnabled(false);
        }
    }
}

void Tab4TaskConfig::onFlowPickup()
{
    if (flow_running_) {
        appendLog("warn", "取电: 已有任务在跑, 先 ⏸ 停止");
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog("error", "取电: RPC 未连接");
        return;
    }
    appendLog("info", "▶ 取电 (phase1, stage 1~5) 启动");
    flow_widget_->resetAll();
    flow_simulating_         = false;
    flow_running_            = true;
    flow_real_sequential_    = true;
    flow_real_stage_idx_     = -1;
    flow_real_end_stage_idx_ = 4;   // 含 → 跑到 stage idx 4 (即第 5 个) 就停
    btn_flow_start_->setEnabled(false);
    btn_flow_stop_->setEnabled(true);
    if (!step_advance_timer_) {
        step_advance_timer_ = new QTimer(this);
        step_advance_timer_->setInterval(1500);
        connect(step_advance_timer_, &QTimer::timeout,
                this, &Tab4TaskConfig::onFlowStepAdvance);
    }
    if (!startRealStage(0)) {
        appendLog("warn", "取电: stage 1~5 全部没有录制脚本");
        flow_real_sequential_    = false;
        flow_real_end_stage_idx_ = -1;
        flow_running_            = false;
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setEnabled(false);
    }
}

void Tab4TaskConfig::onFlowSwap()
{
    if (flow_running_) {
        appendLog("warn", "换电: 已有任务在跑, 先 ⏸ 停止");
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog("error", "换电: RPC 未连接");
        return;
    }
    appendLog("info", "▶ 换电 (phase2, stage 6~9) 启动");
    flow_widget_->resetAll();
    flow_simulating_         = false;
    flow_running_            = true;
    flow_real_sequential_    = true;
    flow_real_stage_idx_     = -1;
    flow_real_end_stage_idx_ = 8;   // 含 → 跑到 stage idx 8 (即第 9 个) 就停
    btn_flow_start_->setEnabled(false);
    btn_flow_stop_->setEnabled(true);
    if (!step_advance_timer_) {
        step_advance_timer_ = new QTimer(this);
        step_advance_timer_->setInterval(1500);
        connect(step_advance_timer_, &QTimer::timeout,
                this, &Tab4TaskConfig::onFlowStepAdvance);
    }
    if (!startRealStage(5)) {
        appendLog("warn", "换电: stage 6~9 全部没有录制脚本");
        flow_real_sequential_    = false;
        flow_real_end_stage_idx_ = -1;
        flow_running_            = false;
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setEnabled(false);
    }
}

// 实机串行模式专用 — 从 from_idx 开始往后找第一个有录制脚本的 stage 装上去
// 并跑起来. 没找到返回 false (表示流程到底了, 收摊).
bool Tab4TaskConfig::startRealStage(int from_idx)
{
    const auto &stages = TaskFlowWidget::stages();
    // 取电 / 换电 设了上界, 不能越界找下一个有脚本的 stage; -1 = 无上界.
    const int search_end = (flow_real_end_stage_idx_ >= 0)
                              ? qMin<int>(flow_real_end_stage_idx_ + 1, stages.size())
                              : stages.size();
    for (int i = from_idx; i < search_end; ++i) {
        const QString sid = stages[i].id;
        const auto it = stage_scripts_.constFind(sid);
        if (it == stage_scripts_.constEnd() || it.value().isEmpty()) {
            appendLog("info",
                QString("[实机] 跳过 stage %1 (%2): 无录制脚本")
                    .arg(i + 1).arg(stages[i].title));
            continue;
        }
        flow_real_stage_idx_      = i;
        flow_step_selected_stage_ = sid;
        flow_step_states_to_run_  = TaskFlowWidget::statesInStage(sid);
        flow_step_script_         = it.value();
        flow_step_use_script_     = true;
        flow_step_run_idx_        = 0;
        flow_step_prev_joints_.clear();

        flow_widget_->setSelectedStage(sid);
        if (!flow_step_states_to_run_.isEmpty()) {
            flow_widget_->setCurrentState(flow_step_states_to_run_.first());
        }
        flow_status_label_->setText(
            QString("[实机] %1/%2 · %3 (%4 步)")
                .arg(i + 1).arg(stages.size()).arg(stages[i].title)
                .arg(flow_step_script_.size()));
        appendLog("info",
            QString("[实机] 进入 stage %1/%2 — %3 (%4 步)")
                .arg(i + 1).arg(stages.size()).arg(stages[i].title)
                .arg(flow_step_script_.size()));

        // 立刻打第 0 步, timer 之后逐步推
        onFlowStepAdvance();
        step_advance_timer_->start();
        return true;
    }
    // 找不到下一个 → 全跑完了
    flow_real_sequential_     = false;
    flow_real_stage_idx_      = -1;
    flow_real_end_stage_idx_  = -1;
    if (step_advance_timer_) step_advance_timer_->stop();
    flow_running_ = false;
    btn_flow_start_->setEnabled(true);
    btn_flow_stop_->setEnabled(false);
    flow_status_label_->setText("[实机] 全部 stage 跑完 ✓");
    appendLog("info", "[实机] ✓ 9 个 stage 全部跑完");
    return false;
}

void Tab4TaskConfig::onFlowStop()
{
    // 单步模式: 复用"停止"按钮做"跳过当前选中步"
    if (mode_step_radio_ && mode_step_radio_->isChecked()) {
        onFlowStepSkip();
        return;
    }
    if (!flow_running_) return;
    appendLog("info", "停止任务流程");
    if (flow_simulating_) {
        flow_sim_timer_->stop();
        // Resume the live angle poll so the viewer tracks the real arm
        // again once the simulator stops driving it.
        if (sim_worker_) {
            QMetaObject::invokeMethod(sim_worker_, "startAnglePolling",
                                      Qt::QueuedConnection, Q_ARG(int, 200));
        }
    } else if (flow_real_sequential_) {
        // 实机串行模式: 停步进 timer, 清状态
        if (step_advance_timer_) step_advance_timer_->stop();
        flow_real_sequential_     = false;
        flow_real_stage_idx_      = -1;
        flow_real_end_stage_idx_  = -1;
        flow_step_run_idx_        = -1;
        flow_step_use_script_     = false;
        flow_step_script_.clear();
        if (rpc_ && rpc_->isConnected()) {
            rpc_->call(Protocol::Methods::ARM_STOP, QJsonObject{}, nullptr);
        }
    } else {
        // (legacy proc_battery_swap 路径,留着以备将来回切)
        swap_poll_timer_->stop();
        rpc_->call("swap.cancel", QJsonObject{}, nullptr);
    }
    flow_running_ = false;
    btn_flow_start_->setEnabled(true);
    btn_flow_stop_->setEnabled(false);
    flow_status_label_->setText("已停止");
}

void Tab4TaskConfig::onFlowReset()
{
    if (flow_running_) onFlowStop();
    flow_widget_->resetAll();
    sim_seg_idx_ = -1;
    sim_playlist_.clear();
    flow_status_label_->setText(QString("就绪 · 模式: %1")
                                   .arg(mode_sim_radio_->isChecked() ? "模拟" : "实机"));
}

// 导出: 把当前的 9-stage 脚本 bundle 写到操作员选的 JSON 文件
void Tab4TaskConfig::onFlowExport()
{
    if (flow_running_) {
        QMessageBox::warning(this, QStringLiteral("运行中"),
                              QStringLiteral("流程运行中, 请先停止再导出"));
        return;
    }
    const QString default_name =
        QStringLiteral("task_%1.json")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出任务配置"),
        QDir::homePath() + "/" + default_name,
        QStringLiteral("Task JSON (*.json)"));
    if (path.isEmpty()) return;

    TaskConfig cfg;
    cfg.scripts = stage_scripts_;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
            QStringLiteral("写入 %1 失败: %2").arg(path, f.errorString()));
        return;
    }
    f.write(QJsonDocument(cfg.toJson()).toJson(QJsonDocument::Indented));
    f.close();

    int total_steps = 0;
    for (const auto &v : stage_scripts_) total_steps += v.size();
    appendLog("info",
        QString("[stages] ✓ 导出 %1 stage / %2 步 → %3")
            .arg(stage_scripts_.size()).arg(total_steps).arg(path));
    flow_status_label_->setText(
        QString("已导出: %1").arg(QFileInfo(path).fileName()));
}

// 加载: 从操作员选的 JSON 替换全部 stage 脚本
void Tab4TaskConfig::onFlowImport()
{
    if (flow_running_) {
        QMessageBox::warning(this, QStringLiteral("运行中"),
                              QStringLiteral("流程运行中, 请先停止再加载"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载任务配置"), QDir::homePath(),
        QStringLiteral("Task JSON (*.json)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("加载失败"),
            QStringLiteral("打开 %1 失败: %2").arg(path, f.errorString()));
        return;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, QStringLiteral("加载失败"),
            QStringLiteral("JSON 解析错误: %1").arg(err.errorString()));
        return;
    }
    const TaskConfig cfg = TaskConfig::fromJson(doc.object());
    if (cfg.scripts.isEmpty()) {
        const auto reply = QMessageBox::question(this, QStringLiteral("空配置"),
            QStringLiteral("此 JSON 不含任何 stage 脚本, 仍要应用 (清空当前所有脚本)?"));
        if (reply != QMessageBox::Yes) return;
    } else {
        int total_steps = 0;
        for (const auto &v : cfg.scripts) total_steps += v.size();
        const auto reply = QMessageBox::question(this, QStringLiteral("确认加载"),
            QStringLiteral("从 %1 加载 %2 个 stage / %3 步, 覆盖当前配置?")
                .arg(QFileInfo(path).fileName())
                .arg(cfg.scripts.size())
                .arg(total_steps));
        if (reply != QMessageBox::Yes) return;
    }
    stage_scripts_ = cfg.scripts;

    // 顺便落盘到 home 配置, 下次启动也用这份
    TaskConfig persisted;
    persisted.scripts = stage_scripts_;
    persisted.saveToHomeFile();

    int total_steps = 0;
    for (const auto &v : stage_scripts_) total_steps += v.size();
    appendLog("info",
        QString("[stages] ✓ 加载 %1 stage / %2 步 ← %3")
            .arg(stage_scripts_.size()).arg(total_steps).arg(path));
    flow_status_label_->setText(
        QString("已加载: %1").arg(QFileInfo(path).fileName()));
}

void Tab4TaskConfig::onFlowSimTick()
{
    if (!flow_simulating_ || sim_seg_idx_ < 0
        || sim_seg_idx_ >= sim_playlist_.size()) return;

    const SimSegment &cur = sim_playlist_[sim_seg_idx_];
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now_ms - flow_sim_step_start_ms_;
    const double t = qBound(0.0, double(elapsed) / double(flow_sim_step_dur_ms_), 1.0);
    // Smooth ease-in-out so the 3D motion doesn't look mechanical.
    const double s = (t < 0.5) ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;

    // Interpolate joint angles into the viewer. Segments with empty
    // target_joints (GRIPPER/DWELL/AIRPORT_*) just hold the previous
    // pose — viewer stays at flow_sim_start_joints_.
    if (viewer_3d_ && flow_sim_start_joints_.size() == 6) {
        QVector<float> live = flow_sim_start_joints_;
        if (cur.target_joints.size() == 6) {
            for (int i = 0; i < 6; ++i) {
                live[i] = float(flow_sim_start_joints_[i] +
                                (cur.target_joints[i] - flow_sim_start_joints_[i]) * s);
            }
        }
        viewer_3d_->setJointAngles(live);
    }

    if (t < 1.0) return;

    // Segment complete — settle pose at the target, mark stage state
    // progress, then advance.
    if (cur.target_joints.size() == 6) {
        flow_sim_start_joints_ = cur.target_joints;
    }
    // For fine-state-mode segments we mark per-state; for script-mode
    // segments we'll mark the whole stage finished when its last segment
    // ends (handled below at stage-boundary).
    if (!cur.is_script_step && !cur.state_id.isEmpty()) {
        flow_widget_->markFinished(cur.state_id);
    }

    // Detect stage boundary: if the next segment is in a different
    // stage (or there is no next), mark all of the current stage's
    // fine-states as Done. This is the only place script-mode segments
    // touch the flow-chart status.
    const QString cur_stage = cur.stage_id;
    const bool last_seg = (sim_seg_idx_ + 1 >= sim_playlist_.size());
    const bool stage_boundary = last_seg
        || sim_playlist_[sim_seg_idx_ + 1].stage_id != cur_stage;
    if (stage_boundary && !cur_stage.isEmpty()) {
        for (const QString &sid : TaskFlowWidget::statesInStage(cur_stage)) {
            flow_widget_->markFinished(sid);
        }
    }

    sim_seg_idx_++;
    if (sim_seg_idx_ >= sim_playlist_.size()) {
        appendLog("info",
            QString("模拟运行完成 — 共 %1 段").arg(sim_playlist_.size()));
        flow_sim_timer_->stop();
        if (sim_worker_) {
            QMetaObject::invokeMethod(sim_worker_, "startAnglePolling",
                                      Qt::QueuedConnection, Q_ARG(int, 200));
        }
        flow_running_ = false;
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setEnabled(false);
        flow_status_label_->setText("模拟完成");
        sim_seg_idx_ = -1;
        return;
    }

    const SimSegment &next = sim_playlist_[sim_seg_idx_];
    flow_sim_step_dur_ms_   = next.duration_ms;
    flow_sim_step_start_ms_ = now_ms;
    if (!next.state_id.isEmpty()) {
        flow_widget_->setCurrentState(next.state_id);
    }
    // Stage entry log when crossing into a new stage.
    if (next.stage_id != cur.stage_id) {
        const int script_size =
            stage_scripts_.contains(next.stage_id)
                ? stage_scripts_[next.stage_id].size() : 0;
        if (script_size > 0) {
            appendLog("info",
                QString("[sim] 进入 stage %1 (脚本 %2 步)")
                    .arg(next.stage_id).arg(script_size));
        }
    }
    flow_status_label_->setText(QString("模拟运行 · %1/%2 · %3")
                                   .arg(sim_seg_idx_ + 1)
                                   .arg(sim_playlist_.size())
                                   .arg(next.label));
}

void Tab4TaskConfig::onSwapStatusPoll()
{
    if (!flow_running_ || flow_simulating_) return;

    rpc_->call("swap.get_state", QJsonObject{},
        [this](QJsonObject reply) {
            if (reply.value("ok").toBool(true) == false &&
                reply.contains("error")) {
                // Service not running yet (proc_battery_swap to be built later).
                const QString err = reply.value("error").toString();
                flow_status_label_->setText(QString("等待 swap 服务: %1").arg(err));
                return;
            }
            const QString cur = reply.value("current_state").toString();
            const QString err = reply.value("error").toString();
            const QJsonArray hist = reply.value("state_history").toArray();

            // Mark every state in history as done.
            for (const QJsonValue &v : hist) {
                const QString sid = v.toObject().value("state").toString();
                if (!sid.isEmpty()) flow_widget_->markFinished(sid);
            }
            if (!cur.isEmpty()) flow_widget_->setCurrentState(cur);
            if (!err.isEmpty()) {
                flow_widget_->markError(cur, err);
                flow_status_label_->setText(QString("失败 @ %1 : %2").arg(cur, err));
            } else {
                const QString sub = reply.value("sub_progress").toString();
                flow_status_label_->setText(
                    QString("运行中 · %1%2")
                       .arg(cur)
                       .arg(sub.isEmpty() ? "" : QString(" · %1").arg(sub)));
            }
        });
}

void Tab4TaskConfig::onFlowStationClicked(QString state_id)
{
    // 单步模式: 点击信号行解析出它所属的 stage(整张卡片), 高亮整卡片.
    // 其他模式: 已经在 TaskFlowWidget 那一层禁用了点击, 不会到这里.
    if (mode_step_radio_ && mode_step_radio_->isChecked()) {
        const QString stage_id = TaskFlowWidget::stageOfState(state_id);
        if (stage_id.isEmpty()) return;

        flow_step_selected_stage_ = stage_id;
        flow_step_states_to_run_  = TaskFlowWidget::statesInStage(stage_id);
        if (flow_widget_) flow_widget_->setSelectedStage(stage_id);

        // Find this stage's title for the status bar.
        QString stage_title = stage_id;
        for (const auto &st : TaskFlowWidget::stages()) {
            if (st.id == stage_id) { stage_title = st.title; break; }
        }
        flow_status_label_->setText(
            QString("单步: 选中 [%1] · 含 %2 个子状态 · 点 ▶ 全部执行")
                .arg(stage_title)
                .arg(flow_step_states_to_run_.size()));
        appendLog("info",
            QString("选中步骤: %1 (含 %2 个子状态)")
                .arg(stage_title).arg(flow_step_states_to_run_.size()));
        return;
    }

    // 兜底 (基本不会走到): 仅日志
    for (const auto &s : TaskFlowWidget::states()) {
        if (s.id == state_id) {
            appendLog("info", QString("点击节点: %1 (%2)").arg(s.label, s.desc));
            return;
        }
    }
}

// ── 单步模式: 把选中 stage 下所有 sub-state 依次执行 ─────────────────
void Tab4TaskConfig::onFlowStepExecute()
{
    if (flow_step_selected_stage_.isEmpty() || flow_step_states_to_run_.isEmpty()) {
        appendLog("warn", "请先在流程图中点击一张卡片选取整个步骤");
        flow_status_label_->setText("单步: ⚠ 未选中任何步骤");
        return;
    }
    if (!rpc_ || !rpc_->isConnected()) {
        appendLog("error", "RPC 未连接, 无法执行");
        return;
    }
    if (flow_step_run_idx_ >= 0) {
        appendLog("warn", "上一次单步还没跑完, 请等待或按 停止");
        return;
    }

    // Choose execution mode:
    //   - If the selected stage has a recorded script (via the ⚙
    //     stage-config dialog), walk THE SCRIPT step-by-step. Every
    //     step type fires (MOVE_JOINTS / GRIPPER / DWELL / etc.).
    //   - Otherwise fall back to the legacy fine-state walker with
    //     hardcoded demo poses.
    // 方案A: 逐 stage 执行只跑录制脚本,与"整套实机运行"完全一致。没有录制
    // 脚本的 stage 直接跳过(不再回退播放硬编码 demo 动作)——否则"单独执行"
    // 会播 demo 而"整套运行"会跳过该 stage,导致两者流程对不上。
    if (!(stage_scripts_.contains(flow_step_selected_stage_) &&
          !stage_scripts_[flow_step_selected_stage_].isEmpty())) {
        appendLog("warn",
            QString("跳过 [%1]: 无录制脚本 (逐 stage 执行只跑录制脚本, 与整套运行一致; "
                    "请在 ⚙ stage 配置对话框里录制/保存步骤)")
                .arg(flow_step_selected_stage_));
        flow_status_label_->setText(
            QString("单步: ⚠ [%1] 无录制脚本, 已跳过").arg(flow_step_selected_stage_));
        return;
    }
    flow_step_use_script_ = true;
    flow_step_script_     = stage_scripts_[flow_step_selected_stage_];
    appendLog("info",
        QString("使用录制脚本 [%1] (含 %2 步,包含所有类型)")
            .arg(flow_step_selected_stage_).arg(flow_step_script_.size()));
    flow_step_run_idx_ = 0;
    flow_step_prev_joints_.clear();
    const int total = flow_step_use_script_
                          ? flow_step_script_.size()
                          : flow_step_states_to_run_.size();
    flow_status_label_->setText(
        QString("单步执行 [%1]  1/%2").arg(flow_step_selected_stage_).arg(total));

    if (!step_advance_timer_) {
        step_advance_timer_ = new QTimer(this);
        step_advance_timer_->setInterval(1500);  // per-substate
        connect(step_advance_timer_, &QTimer::timeout,
                this, &Tab4TaskConfig::onFlowStepAdvance);
    }

    // Fire substate 0 right now, then start the timer for the rest.
    onFlowStepAdvance();
    step_advance_timer_->start();

    btn_flow_start_->setEnabled(false);
    btn_flow_stop_->setText("⏹ 停止单步");
}

void Tab4TaskConfig::onFlowStepAdvance()
{
    const int total = flow_step_use_script_
                          ? flow_step_script_.size()
                          : flow_step_states_to_run_.size();
    if (flow_step_run_idx_ < 0 || flow_step_run_idx_ >= total) {
        // 当前 stage 的脚本跑完 — 把它的所有 fine-state 标 Done
        for (const QString &sid : flow_step_states_to_run_) {
            flow_widget_->markFinished(sid);
        }
        if (step_advance_timer_) step_advance_timer_->stop();
        appendLog("info", QString("✓ 步骤完成: %1").arg(flow_step_selected_stage_));

        // 实机模式: 自动跳到下一个 stage. 单步模式: 收摊.
        if (flow_real_sequential_) {
            const int next = flow_real_stage_idx_ + 1;
            flow_step_run_idx_ = -1;
            flow_step_use_script_ = false;
            flow_step_script_.clear();
            // 取电 / 换电 按钮设了上界 (含). 越界就收尾, 不再下钻.
            if (flow_real_end_stage_idx_ >= 0 && next > flow_real_end_stage_idx_) {
                appendLog("info",
                    QString("[实机] ✓ 阶段段落跑完 (stage %1..%2)")
                        .arg(flow_real_stage_idx_ + 1)   // 这里 stage_idx 还是当前完成的
                        .arg(flow_real_end_stage_idx_ + 1));
                flow_real_sequential_     = false;
                flow_real_stage_idx_      = -1;
                flow_real_end_stage_idx_  = -1;
                if (step_advance_timer_) step_advance_timer_->stop();
                flow_running_ = false;
                btn_flow_start_->setEnabled(true);
                btn_flow_stop_->setEnabled(false);
                flow_status_label_->setText("[实机] ✓ 段落完成");
                return;
            }
            if (startRealStage(next)) {
                return;   // startRealStage 已经把下一个 stage 的第一步打出去 + timer 启了
            }
            // 全部跑完, startRealStage 已经收尾
            return;
        }

        flow_step_run_idx_ = -1;
        flow_step_use_script_ = false;
        flow_step_script_.clear();
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setText("⏭ 跳过(标完成)");
        flow_status_label_->setText(
            QString("✓ 步骤 %1 执行完成").arg(flow_step_selected_stage_));
        return;
    }

    // ── Script-mode branch: dispatch every step type ─────────────────
    if (flow_step_use_script_) {
        dispatchScriptStep(flow_step_script_[flow_step_run_idx_]);
        ++flow_step_run_idx_;
        return;
    }

    const QString sid = flow_step_states_to_run_[flow_step_run_idx_];
    const auto &states = TaskFlowWidget::states();
    int s_idx = -1;
    for (int i = 0; i < states.size(); ++i) {
        if (states[i].id == sid) { s_idx = i; break; }
    }
    if (s_idx < 0 || states[s_idx].demo_joints_deg.size() != 6) {
        appendLog("warn", "跳过(无效子状态): " + sid);
        ++flow_step_run_idx_;
        return;
    }

    const TaskState &s = states[s_idx];

    // Per-stage configured-script overlay (mirrors what onFlowSimTick
    // does for the sim path). If the operator recorded a TaskStep
    // script via the ⚙ stage-config dialog, prefer that over the
    // hardcoded demo poses. Before this overlay, "执行选中" silently
    // ran the canned TaskFlowWidget demo poses regardless of what the
    // operator had recorded — the recorded points were only honoured
    // in simulation mode.
    QVector<float> target_joints = s.demo_joints_deg;
    double speed_ratio = 0.30;    // default 30%
    const QString stage_id = TaskFlowWidget::stageOfState(sid);
    if (!stage_id.isEmpty() && stage_scripts_.contains(stage_id)) {
        const QVector<QString> stage_states = TaskFlowWidget::statesInStage(stage_id);
        const int pos_in_stage = stage_states.indexOf(sid);
        const auto &script = stage_scripts_[stage_id];
        QVector<int> mj_step_indices;
        for (int si = 0; si < script.size(); ++si) {
            if (script[si].type == StepType::MOVE_JOINTS) mj_step_indices.append(si);
        }
        if (pos_in_stage >= 0 && pos_in_stage < mj_step_indices.size()) {
            const TaskStep &step = script[mj_step_indices[pos_in_stage]];
            const QVariantList j = step.params.value("joints").toList();
            if (j.size() == 6) {
                target_joints.resize(6);
                for (int i = 0; i < 6; ++i) target_joints[i] = float(j[i].toDouble());
                speed_ratio = step.params.value("speed_ratio", 0.30).toDouble();
                if (pos_in_stage == 0) {
                    appendLog("info", QString("使用录制脚本 [%1] (含 %2 步)")
                                         .arg(stage_id).arg(script.size()));
                }
            }
        }
    }

    QJsonObject params;
    QJsonArray jarr;
    for (float v : target_joints) jarr.append(v);
    params[Protocol::Fields::JOINTS] = jarr;
    params["speed_ratio"] = speed_ratio;
    rpc_->call(Protocol::Methods::ARM_MOVE_JOINTS, params,
        [this](QJsonObject reply) {
            if (!reply.value("ok").toBool(true)) {
                const QString err = reply.value("error").toString();
                appendLog("warn", QString("⚠ move_joints 拒绝: %1").arg(err));
            }
        });

    // Dynamically extend timer interval based on actual joint delta.
    // Hardcoded 1500 ms was too short for >30° moves at 30% speed —
    // the next tick fired before the arm reached, target got
    // overwritten, arm landed visibly short. Estimate from speed_ratio
    // (Piper at 100% ≈ 60°/s, so deg_per_s ≈ 60 * speed_ratio).
    if (step_advance_timer_) {
        float max_delta = 0.0f;
        if (!flow_step_prev_joints_.isEmpty()) {
            for (int i = 0; i < 6 && i < flow_step_prev_joints_.size() && i < target_joints.size(); ++i) {
                max_delta = std::max(max_delta, std::abs(target_joints[i] - flow_step_prev_joints_[i]));
            }
        }
        const float deg_per_s = float(60.0 * std::max(0.05, speed_ratio));
        const int travel_ms = int(max_delta * 1000.0f / deg_per_s);
        const int dyn_step_ms = std::max(1500, travel_ms + 500);
        step_advance_timer_->setInterval(dyn_step_ms);
        if (max_delta > 0.0f) {
            appendLog("info", QString("  travel %1°, speed_ratio=%2 → step %3 ms")
                                  .arg(max_delta, 0, 'f', 1)
                                  .arg(speed_ratio, 0, 'f', 2)
                                  .arg(dyn_step_ms));
        }
    }
    flow_step_prev_joints_ = target_joints;   // remember for next step's delta

    flow_widget_->setCurrentState(s.id);

    appendLog("info", QString("▶ 子状态 %1/%2: %3")
                        .arg(flow_step_run_idx_ + 1)
                        .arg(flow_step_states_to_run_.size())
                        .arg(s.label));
    flow_status_label_->setText(
        QString("单步执行 [%1]  %2/%3  %4")
            .arg(flow_step_selected_stage_)
            .arg(flow_step_run_idx_ + 1)
            .arg(flow_step_states_to_run_.size())
            .arg(s.label));

    ++flow_step_run_idx_;
}

// Dispatch one recorded TaskStep to the appropriate RPC, set the
// step_advance_timer_ interval to match the step's expected duration,
// and log what we're doing so the operator can follow along.
//
// Supported types:
//   MOVE_JOINTS     → arm.move_joints  (dynamic ms by joint delta)
//   MOVE_CARTESIAN  → piper.move_cartesian  (~3s default)
//   GRIPPER         → piper.set_gripper_angle (~800ms)
//   DWELL           → no RPC, just wait `ms` ms
//   AIRPORT_GRIPPER → airport.lock / airport.release  (~2s)
//   AIRPORT_RAIL    → airport.lock or airport.release; backend stall
//                     monitor cuts the motor on jam; GUI waits
//                     step.max_ms before advancing.
//   WAIT_DETECT_*   → currently skipped with a log (TODO: poll)
void Tab4TaskConfig::dispatchScriptStep(const TaskStep &step)
{
    const int step_num = flow_step_run_idx_ + 1;
    const int total    = flow_step_script_.size();
    const QString note = step.label.isEmpty()
        ? QString()
        : QStringLiteral(" — %1").arg(step.label);
    int duration_ms = 1500;   // default per-step duration

    auto cb_log_err = [this, step_num](QJsonObject reply) {
        if (!reply.value("ok").toBool(true)) {
            appendLog("warn",
                QString("⚠ 第 %1 步 RPC 拒绝: %2")
                    .arg(step_num).arg(reply.value("error").toString()));
        }
    };

    switch (step.type) {
        case StepType::MOVE_JOINTS: {
            const QVariantList j = step.params.value("joints").toList();
            const double speed_ratio = step.params.value("speed_ratio", 0.30).toDouble();
            if (j.size() != 6) {
                appendLog("warn", QString("第 %1 步 MOVE_JOINTS 参数缺关节, 跳过").arg(step_num));
                break;
            }
            QJsonObject p;
            QJsonArray jarr;
            QVector<float> target_joints(6);
            for (int i = 0; i < 6; ++i) {
                target_joints[i] = float(j[i].toDouble());
                jarr.append(target_joints[i]);
            }
            p[Protocol::Fields::JOINTS] = jarr;
            p["speed_ratio"] = speed_ratio;
            rpc_->call(Protocol::Methods::ARM_MOVE_JOINTS, p, cb_log_err);

            float max_delta = 0.0f;
            if (!flow_step_prev_joints_.isEmpty()) {
                for (int i = 0; i < 6 && i < flow_step_prev_joints_.size(); ++i) {
                    max_delta = std::max(max_delta, std::abs(target_joints[i] - flow_step_prev_joints_[i]));
                }
            }
            const float deg_per_s = float(60.0 * std::max(0.05, speed_ratio));
            const int travel_ms = int(max_delta * 1000.0f / deg_per_s);
            duration_ms = std::max(1500, travel_ms + 500);
            flow_step_prev_joints_ = target_joints;
            appendLog("info",
                QString("▶ [%1/%2] MOVE_JOINTS 到 %3 (Δ%4°, %5ms)%6")
                    .arg(step_num).arg(total)
                    .arg(QString("[%1]").arg([&](){
                        QStringList ss; for (int i = 0; i < 6; ++i) ss << QString::number(target_joints[i], 'f', 1);
                        return ss.join(", ");
                    }()))
                    .arg(max_delta, 0, 'f', 1)
                    .arg(duration_ms)
                    .arg(note));
            break;
        }
        case StepType::GRIPPER: {
            const double angle_mm  = step.params.value("angle_mm").toDouble();
            const int    force_pct = step.params.value("force_pct").toInt();
            QJsonObject p;
            p["angle_mm"]   = angle_mm;
            p["effort_mNm"] = double(force_pct) * 20.0;   // 0-100% → 0-2000 mN·m
            rpc_->call(Protocol::Methods::PIPER_SET_GRIPPER_ANGLE, p, cb_log_err);
            duration_ms = 800;   // gripper actuation typically <500ms
            appendLog("info",
                QString("▶ [%1/%2] GRIPPER → %3mm @ %4%%5")
                    .arg(step_num).arg(total)
                    .arg(angle_mm, 0, 'f', 1)
                    .arg(force_pct)
                    .arg(note));
            break;
        }
        case StepType::DWELL: {
            const int ms = step.params.value("ms", 1000).toInt();
            duration_ms = std::max(100, ms);
            appendLog("info",
                QString("▶ [%1/%2] DWELL %3ms%4")
                    .arg(step_num).arg(total).arg(ms).arg(note));
            break;
        }
        case StepType::FIX_POINT: {
            // 定点跟踪: send arm.move_cartesian to the recorded TCP, then dwell.
            // While dwelling, the controller holds the TCP at target — the
            // operator's "always point at this spot for N seconds" semantic.
            const double x  = step.params.value("x_mm").toDouble();
            const double y  = step.params.value("y_mm").toDouble();
            const double z  = step.params.value("z_mm", 200.0).toDouble();
            const double rx = step.params.value("rx_deg").toDouble();
            const double ry = step.params.value("ry_deg", 85.0).toDouble();
            const double rz = step.params.value("rz_deg").toDouble();
            const int duration = std::max(200, step.params.value("duration_ms", 5000).toInt());

            // proc_piper.m_piper_move_cartesian expects UPPERCASE X_mm/Y_mm
            // /Z_mm + RX_deg/RY_deg/RZ_deg (lowercase rejects as missing
            // args). The legacy proc_arm methods used lowercase + roll/pitch
            // /yaw, which is a different path.
            QJsonObject p;
            p["X_mm"]   = x;  p["Y_mm"]   = y;  p["Z_mm"]   = z;
            p["RX_deg"] = rx; p["RY_deg"] = ry; p["RZ_deg"] = rz;
            p["mode"]   = "P";
            rpc_->call(Protocol::Methods::PIPER_MOVE_CARTESIAN, p, cb_log_err);

            // duration_ms = travel-to-point time + the user's hold time.
            // Use a fixed 1500 ms allowance for the move-to phase (Piper's
            // typical max for a Cartesian step at default speed); the rest
            // is pure dwell at the target.
            duration_ms = 1500 + duration;
            appendLog("info",
                QString("▶ [%1/%2] 定点跟踪 (%3, %4, %5)mm RPY=(%6, %7, %8)° 保持 %9ms%10")
                    .arg(step_num).arg(total)
                    .arg(x, 0, 'f', 1).arg(y, 0, 'f', 1).arg(z, 0, 'f', 1)
                    .arg(rx, 0, 'f', 1).arg(ry, 0, 'f', 1).arg(rz, 0, 'f', 1)
                    .arg(duration).arg(note));
            break;
        }
        case StepType::AIRPORT_GRIPPER: {
            // GPIO 继电器夹爪 (AirportWidget 的 张开/夹紧 按钮也是这条)。
            // 之前错调成了 airport.lock / airport.release — 那是导轨 1+3
            // 平台夹紧, 跟继电器没关系。
            const bool open = step.params.value("open").toBool();
            QJsonObject p;
            p[Protocol::Fields::OPEN] = open;
            rpc_->call(Protocol::Methods::AIRPORT_GRIPPER, p, cb_log_err);
            duration_ms = 800;
            appendLog("info",
                QString("▶ [%1/%2] 机场夹爪 (继电器) %3%4")
                    .arg(step_num).arg(total).arg(open ? "张开" : "夹紧").arg(note));
            break;
        }
        case StepType::AIRPORT_RAIL: {
            const QString action    = step.params.value("action", "lock").toString();
            const QString stop_mode = step.params.value("stop_mode", "stall").toString();
            const int speed_rpm     = step.params.value("speed_rpm", 1500).toInt();
            const double distance   = step.params.value("distance_mm", 50.0).toDouble();
            const int max_ms        = std::max(500, step.params.value("max_ms", 7000).toInt());
            constexpr int kStallSettleMs = 300;

            QString name;
            QString stop_method = Protocol::Methods::AIRPORT_STOP_ALL;
            QJsonObject stop_params;
            QVector<int> watched_rails;   // backend indices this step drives
            int direction = +1;           // +1 forward / -1 backward (for distance mode)

            if (action == "release") {
                name = "释放(1+3)";
                watched_rails = {0, 2};
                direction = -1;           // pair release moves opposite of lock
            } else if (action == "rail2_fwd" || action == "rail2_back") {
                stop_method = Protocol::Methods::AIRPORT_STOP;
                stop_params[Protocol::Fields::RAIL] = 1;
                name = (action == "rail2_fwd") ? "导轨2 前进" : "导轨2 后退";
                watched_rails = {1};
                direction = (action == "rail2_fwd") ? +1 : -1;
            } else {
                name = "锁定(1+3)";
                watched_rails = {0, 2};
                direction = +1;
            }

            rpc_->call(stop_method, stop_params);

            // Distance mode now uses the gateway's CLOSED-LOOP airport.move_mm
            // (velocity + 0x36 encoder feedback) which stops at the EXACT
            // target distance — so this is no longer a stop timer, only a
            // step-duration estimate (how long to wait before advancing).
            // Empirically counts/s ≈ rpm × 176.6 at 62922 counts/mm, so the
            // move takes ≈ dist × 356300 / rpm ms; use 380000 for margin.
            const int distance_time_ms = (stop_mode == "distance" && speed_rpm > 0)
                ? std::max(300, int(std::abs(distance) * 380000.0 / double(std::abs(speed_rpm))))
                : 0;

            const QVector<int> rails_copy = watched_rails;
            QTimer::singleShot(kStallSettleMs, this,
                [this, action, stop_mode, speed_rpm, direction, distance,
                 rails_copy, cb_log_err]() {
                    if (!rpc_) return;
                    if (stop_mode == "distance") {
                        // Distance mode = gateway CLOSED-LOOP precise move
                        // (airport.move_mm: velocity + 0x36 encoder feedback,
                        // stops at the exact target). Signed dist_mm carries
                        // the direction. Replaces the old set_speed + timer +
                        // stop (distance ≈ speed×time) which overshot badly
                        // (e.g. 50mm command → 70mm actual).
                        for (int rail_idx : rails_copy) {
                            QJsonObject p;
                            p[Protocol::Fields::RAIL]      = rail_idx;
                            p[Protocol::Fields::DIST_MM]   =
                                std::abs(distance) * (direction >= 0 ? +1.0 : -1.0);
                            p[Protocol::Fields::SPEED_RPM] = std::abs(speed_rpm);
                            rpc_->call(Protocol::Methods::AIRPORT_MOVE_MM, p, cb_log_err);
                        }
                    } else {
                        // stall mode
                        QJsonObject p;
                        p[Protocol::Fields::SPEED_RPM] = speed_rpm;
                        if (action == "release") {
                            rpc_->call(Protocol::Methods::AIRPORT_RELEASE, p, cb_log_err);
                        } else if (action == "rail2_fwd" || action == "rail2_back") {
                            p[Protocol::Fields::RAIL] = 1;
                            p[Protocol::Fields::SPEED_RPM] =
                                (action == "rail2_fwd") ? speed_rpm : -speed_rpm;
                            rpc_->call(Protocol::Methods::AIRPORT_SET_SPEED, p, cb_log_err);
                        } else {
                            rpc_->call(Protocol::Methods::AIRPORT_LOCK, p, cb_log_err);
                        }
                    }
                });

            duration_ms = (stop_mode == "distance")
                ? (kStallSettleMs + distance_time_ms + 500)   // pre-stop + move + 500ms grace
                : (max_ms + kStallSettleMs);

            // Status poll — advance condition depends on stop_mode.
            // 注意 +1: 调用方 (onFlowStepAdvance script 分支) 在 dispatchScriptStep
            // 返回之后立刻 ++flow_step_run_idx_. 所以"我们仍在这一步上"的
            // 当前 run_idx 实际是 dispatch 时的值 + 1. 不加这个 +1, 下面
            // pollAirportRailDone 里的 (flow_step_run_idx_ != session_run_idx)
            // 守卫会立刻为真, poll 永远跑不起来.
            // Status-poll early-advance is ONLY for stall mode (advance the
            // instant the rail stalls). DISTANCE mode must NOT poll: the
            // gateway's airport.move_mm stops at the exact target and we
            // advance on the duration_ms timer. The device gateway has no real
            // airport.get_status — it ACKs unknown methods with {"ok":true}
            // and NO "rails", so for distance mode the poll reads state=IDLE,
            // falsely "completes" immediately, stop()s the step-advance timer,
            // and the whole flow STALLS after this step (the "导轨2 stage 之后
            // 后面 stage 不执行" bug). So skip polling for distance.
            if (stop_mode != "distance") {
                const int session_run_idx = flow_step_run_idx_ + 1;
                QTimer::singleShot(kStallSettleMs + 200, this,
                    [this, watched_rails, stop_mode, name, session_run_idx]() {
                        if (flow_step_run_idx_ != session_run_idx) return;
                        pollAirportRailDone(watched_rails, stop_mode, name, session_run_idx);
                    });
            }

            const QString mode_label = (stop_mode == "distance")
                ? QString("固定 %1mm (~%2ms)").arg(std::abs(distance), 0, 'f', 1).arg(distance_time_ms)
                : QString("堵转停");
            appendLog("info",
                QString("▶ [%1/%2] AIRPORT_RAIL %3 @ %4rpm  %5  (%6ms 急停 + 最长 %7ms)%8")
                    .arg(step_num).arg(total).arg(name).arg(speed_rpm)
                    .arg(mode_label).arg(kStallSettleMs).arg(max_ms).arg(note));
            break;
        }
        case StepType::DOOR:
        case StepType::HELIPAD: {
            // 舱门 / 停机坪 — proc_door over RS485. Motion is asynchronous
            // there: the RPC returns once the coils are set, and proc_door
            // supervises its own limit switches (X3/X4 hatch, X1/X2 lift)
            // and cuts power on arrival or timeout. So we fire the command,
            // then poll door.get_status and advance the instant the axis
            // reports moving=false — max_ms is only the fallback bound.
            const bool is_door = (step.type == StepType::DOOR);
            const QString action =
                step.params.value("action", is_door ? "open" : "up").toString();
            const int max_ms = std::max(500,
                step.params.value("max_ms", is_door ? 20000 : 30000).toInt());

            QString method;
            QString name;
            if (is_door) {
                if (action == "close")      { method = Protocol::Methods::DOOR_CLOSE;   name = "关舱门"; }
                else if (action == "stop")  { method = Protocol::Methods::DOOR_STOP;    name = "舱门停止"; }
                else                        { method = Protocol::Methods::DOOR_OPEN;    name = "开舱门"; }
            } else {
                if (action == "down")       { method = Protocol::Methods::HELIPAD_DOWN; name = "停机坪下降"; }
                else if (action == "stop")  { method = Protocol::Methods::HELIPAD_STOP; name = "停机坪停止"; }
                else                        { method = Protocol::Methods::HELIPAD_UP;   name = "停机坪上升"; }
            }

            rpc_->call(method, QJsonObject{}, cb_log_err);

            if (action == "stop") {
                // De-energising is instantaneous — nothing to wait for.
                duration_ms = 300;
                appendLog("info",
                    QString("▶ [%1/%2] %3%4").arg(step_num).arg(total).arg(name).arg(note));
                break;
            }

            duration_ms = max_ms;

            // +1 for the same reason as AIRPORT_RAIL: the caller bumps
            // flow_step_run_idx_ right after dispatchScriptStep returns, so
            // "still on this step" is the dispatch-time value + 1.
            const int session_run_idx = flow_step_run_idx_ + 1;
            const QString axis_key = is_door ? QStringLiteral("hatch")
                                             : QStringLiteral("helipad");
            // Give proc_door a beat to flip moving=true before the first
            // poll, otherwise we'd read the pre-command idle state and
            // "complete" instantly.
            QTimer::singleShot(400, this,
                [this, axis_key, name, session_run_idx]() {
                    if (flow_step_run_idx_ != session_run_idx) return;
                    pollDoorAxisDone(axis_key, name, session_run_idx);
                });

            appendLog("info",
                QString("▶ [%1/%2] %3 → 等限位 (最长 %4ms)%5")
                    .arg(step_num).arg(total).arg(name).arg(max_ms).arg(note));
            break;
        }
        case StepType::MOVE_CARTESIAN:
        case StepType::WAIT_DETECT_UAV:
        case StepType::WAIT_DETECT_BAT:
            appendLog("warn",
                QString("⚠ [%1/%2] %3 暂未在单步实机中支持, 跳过%4")
                    .arg(step_num).arg(total)
                    .arg(TaskStep::typeLabel(step.type)).arg(note));
            duration_ms = 100;   // skip fast
            break;
    }

    if (step_advance_timer_) step_advance_timer_->setInterval(duration_ms);
}

void Tab4TaskConfig::onFlowStepSkip()
{
    // 执行中: 终止, 当前 stage 全部标完成
    if (flow_step_run_idx_ >= 0) {
        if (step_advance_timer_) step_advance_timer_->stop();
        for (const QString &sid : flow_step_states_to_run_) {
            flow_widget_->markFinished(sid);
        }
        flow_step_run_idx_ = -1;
        btn_flow_start_->setEnabled(true);
        btn_flow_stop_->setText("⏭ 跳过(标完成)");
        appendLog("warn",
            QString("⏹ 终止单步执行 [%1]").arg(flow_step_selected_stage_));
        flow_status_label_->setText(
            QString("已终止 [%1] · 已标全部完成").arg(flow_step_selected_stage_));
        return;
    }

    // 待机中: 把选中 stage 全部标完成不执行
    if (flow_step_selected_stage_.isEmpty() || flow_step_states_to_run_.isEmpty()) {
        appendLog("warn", "请先选中一张卡片再跳过");
        return;
    }
    for (const QString &sid : flow_step_states_to_run_) {
        flow_widget_->markFinished(sid);
    }
    appendLog("info",
        QString("⏭ 跳过(标记完成): %1").arg(flow_step_selected_stage_));
    flow_status_label_->setText(
        QString("已跳过 [%1] · 选下一张卡片继续").arg(flow_step_selected_stage_));
}


// ════════════════════════════════════════════════════════════════════════
// ⚙ Configure stage: open StageConfigDialog, save accepted result to the
// in-memory map AND write the whole bundle out to the user-home JSON.
// ════════════════════════════════════════════════════════════════════════
void Tab4TaskConfig::onStageConfigClicked(QString stage_id)
{
    if (flow_running_) {
        QMessageBox::warning(this, QStringLiteral("配置中"),
                              QStringLiteral("流程运行中, 请先停止再配置 stage"));
        return;
    }

    // Look up stage title for the dialog header (purely cosmetic).
    QString title = stage_id;
    for (const auto &st : TaskFlowWidget::stages()) {
        if (st.id == stage_id) { title = st.title; break; }
    }

    const QVector<TaskStep> existing = stage_scripts_.value(stage_id);

    StageConfigDialog dlg(stage_id, title, existing, rpc_, this);
    // 对话框的 保存 按钮不再关窗口, 它发 saveStage 信号 → 这里写 JSON.
    // 对话框保持打开, 操作员可以继续编辑 / 再次保存 / 验证.
    connect(&dlg, &StageConfigDialog::saveStage, this,
        [this, stage_id](const QVector<TaskStep> &steps) {
            stage_scripts_[stage_id] = steps;
            TaskConfig cfg;
            cfg.scripts = stage_scripts_;
            if (!cfg.saveToHomeFile()) {
                appendLog("error",
                    QString("[stages] 保存失败: %1").arg(TaskConfig::homeFilePath()));
                QMessageBox::warning(this, QStringLiteral("保存失败"),
                    QStringLiteral("写入 %1 失败").arg(TaskConfig::homeFilePath()));
                return;
            }
            appendLog("info",
                QString("[stages] %1 步骤已保存到 %2 (%3 步)")
                    .arg(stage_id)
                    .arg(TaskConfig::homeFilePath())
                    .arg(stage_scripts_[stage_id].size()));
        });
    // 取消 / X 都走 reject, exec() 返回 Rejected, 这里也无需再做什么 —
    // 因为 saveStage 每次都会同步写盘, 所有 "保存过的" 都在 JSON 里了.
    dlg.exec();
}
