#ifndef TAB4TASKCONFIG_H
#define TAB4TASKCONFIG_H

#include <QWidget>
#include <QString>
#include <QVector>
#include <QJsonObject>

class RpcClient;
class LogWidget;
class QListWidget;
class QLabel;
class QPushButton;
class QSplitter;
class QThread;
class QSlider;
class QRadioButton;
class ArmViewer3D;
class ArmSyncWorker;
class TaskFlowWidget;
struct TaskStep;

// Task Configuration Tab
// Layout:
//   ── Horizontal Splitter ──────────────────────────────────────────────────
//   │  Left: 3D visualization placeholder (future: small car+arm+objects)   │
//   │  Right: task list + action buttons + task status                       │
//   ── Bottom: shared LogWidget ─────────────────────────────────────────────
class Tab4TaskConfig : public QWidget {
    Q_OBJECT
public:
    explicit Tab4TaskConfig(RpcClient *rpc, QWidget *parent = nullptr);
    ~Tab4TaskConfig() override;

    void appendLog(const QString &level, const QString &msg);
    void setConnectionParams(const QString &host, quint16 rpc_port, quint16 vid_port);

    // Camera-dock API. The HostGUI has a single CameraWidget that lives on
    // the dashboard tab by default. When the user switches to this tab the
    // top-level MainWindow reparents that widget into our `cam_dock_` slot
    // (where the 3D-sim panel used to live), and moves it back on exit.
    // The two pages never display video simultaneously — the same widget
    // is shuttled between them.
    void mountCamera(QWidget *cam);
    void unmountCamera();

private slots:
    void onStartTask();
    void onStopTask();
    void onResetTask();
    void onEstopTask();          // hard emergency stop - bypasses confirmation
    void onPollTaskStatus();     // periodic poll of task.get_status

    void onStartResult(QJsonObject result);
    void onStopResult(QJsonObject result);

    // Simulation controls — 3D-only, never touches the real arm.
    void onSimulateTask();
    void onSimReturnToLive();
    void onSimTick();

    // Battery-swap pipeline (the metro-style flow chart on the right).
    // Two modes: simulation (3D-only, lerps joints between station poses)
    // and real (polls swap.get_state from proc_battery_swap).
    void onFlowModeChanged();
    void onFlowStart();
    void onFlowStop();
    void onFlowReset();
    void onFlowExport();              // 整套 9 stage 任务脚本另存为 JSON
    void onFlowImport();              // 从 JSON 加载, 替换当前脚本
    // Phase-only shortcuts on the flow bar:
    //   取电: 跑 phase1 (stage 1..5 == idx 0..4)
    //   换电: 跑 phase2 (stage 6..9 == idx 5..8)
    // 共用实机模式那套 dispatchScriptStep + step_advance_timer 机制,
    // 关键差别是 flow_real_end_stage_idx_ 给定上界 (含), 跑到它就停而
    // 不是继续到 stage 9.
    void onFlowPickup();              // 取电: phase1, stage idx 0..4
    void onFlowSwap();                // 换电: phase2, stage idx 5..8
    void onFlowSimTick();             // 30 Hz interpolation when sim is running
    void onSwapStatusPoll();          // 2 Hz polling in real mode
    void onFlowStationClicked(QString state_id);
    void onStageConfigClicked(QString stage_id);   // ⚙ on TaskFlowWidget card

    // ── 配置权限 ─────────────────────────────────────────────────────────
    // Ctrl+L+G → AuthDialog (admin/admin) → the ⚙ buttons become live.
    // Pressing the chord again while unlocked re-locks, so the operator can
    // hand the station back without restarting the app.
    void onConfigLockShortcut();
    void onConfigLockedClick();      // gear clicked while locked → hint

    // 单步调试模式: 流程图上选一张卡片(stage), "执行选中" 把该 stage 下
    // 所有 sub-state 依次通过 arm.move_joints 发到真机, 1.5 秒每一步.
    void onFlowStepExecute();
    void onFlowStepSkip();
    void onFlowStepAdvance();   // QTimer 触发, 把执行索引向后推

private:
    void buildUi();
    // Applies config_unlocked_ to the flow chart + the header indicator.
    // Single place that touches the lock's visible state, so the two can
    // never disagree.
    void applyConfigLockState();
    // Walks one TaskStep from the recorded script, dispatching to the
    // right RPC by step type and setting step_advance_timer_'s interval
    // to match the step's expected duration.
    void dispatchScriptStep(const struct TaskStep &step);
    // 实机模式: 把当前 flow_real_stage_idx_ 指向的 stage 的脚本装进
    // flow_step_script_ / flow_step_states_to_run_, 然后用单步那套 timer 跑.
    // 返回 true 成功开跑, false 表示已经走到 9 个 stage 末尾.
    bool startRealStage(int stage_idx);
    // Poll airport.get_status; advance script step when all watched
    // rails reach the wait condition. session_run_idx captures
    // flow_step_run_idx_ at dispatch time so stale callbacks from a
    // previous step don't mis-advance a later one.
    //   mode = "stall"    → advance when ALL watched rails state == STALLED
    //   mode = "distance" → advance when ALL watched rails state != MOVING
    //                       (i.e. reached IDLE or stalled-early)
    void pollAirportRailDone(const QVector<int> &watched_rails,
                             const QString &mode,
                             const QString &label,
                             int session_run_idx);

    // Same idea for the proc_door axes. Polls door.get_status and advances
    // as soon as the axis reports moving=false — proc_door supervises its
    // own limit switches and cuts power on arrival, so "stopped moving" is
    // the completion signal. axis_key is "hatch" or "helipad".
    void pollDoorAxisDone(const QString &axis_key,
                          const QString &label,
                          int session_run_idx);
    struct SimSegment;
    QVector<SimSegment> buildSimPlaylist() const;
    QWidget* build3DViewer();
    QWidget* buildTaskPanel();
    QWidget* buildSimPanel();
    QWidget* buildCameraDock();      // replaces buildSimPanel() in the layout
    void updateStatusFromBackend(const QJsonObject &status);

    // Wires the sync worker: starts a QThread, asks worker to load STL
    // assets and begin angle polling, and forwards viewer updates.
    void start3DSimThread();
    void stop3DSimThread();
    // Called when the worker fires requestAngles(): does the actual RPC
    // call (sockets must live on the main thread) and forwards the
    // reply back to the worker.
    void onAnglesRequested();

    RpcClient   *rpc_;
    QListWidget *task_list_;
    QPushButton *btn_start_;
    QPushButton *btn_stop_;
    QPushButton *btn_estop_;     // big red emergency stop
    QPushButton *btn_reset_;
    QLabel      *task_status_label_;
    LogWidget   *log_widget_ = nullptr;
    class QTimer *poll_timer_ = nullptr;

    // 3D simulation (all GUI-thread-owned references; worker lives in
    // sim_thread_ via moveToThread).
    ArmViewer3D   *viewer_3d_   = nullptr;
    ArmSyncWorker *sim_worker_  = nullptr;
    QThread       *sim_thread_  = nullptr;

    // Slot the dashboard's CameraWidget reparents into when the user is
    // viewing this tab (replaces the old 3D-sim controls panel).
    QWidget     *cam_dock_       = nullptr;

    // Standalone test controls — verify the 3D model animates without
    // needing the live arm or any RPC connection.
    QSlider     *joint_sliders_[6] = {nullptr};
    QLabel      *joint_value_labels_[6] = {nullptr};
    QPushButton *btn_sim_run_     = nullptr;
    QPushButton *btn_sim_zero_    = nullptr;
    QPushButton *btn_sim_live_    = nullptr;
    QLabel      *sim_status_label_ = nullptr;
    QTimer      *sim_timer_       = nullptr;
    double       sim_t0_ms_       = 0.0;
    bool         sim_mode_active_ = false;
    QVector<QPair<float, QVector<float>>> sim_traj_;

    // ── Battery-swap flow pipeline (right pane) ──────────────────────────
    TaskFlowWidget *flow_widget_      = nullptr;
    QRadioButton   *mode_sim_radio_   = nullptr;
    QRadioButton   *mode_real_radio_  = nullptr;
    QRadioButton   *mode_step_radio_  = nullptr;   // 单步调试 (实机, 一次一步)
    QString         flow_step_selected_stage_;     // 当前选中的"步骤" (stage_id)
    QVector<QString> flow_step_states_to_run_;     // 选中 stage 下所有 state_id, 顺序
    int              flow_step_run_idx_ = -1;      // 当前执行到第几个 (在 to_run_ 或 script 里的索引)

    // 实机模式 (串行跑全部 9 个 stage). 单步是 "选中一个 stage 跑它的脚本",
    // 实机就是 "stage 1..9 一个接一个全跑完", 复用同一套 dispatchScriptStep
    // + step_advance_timer 机制. flow_real_sequential_ = true 时, 当前 stage
    // 的脚本跑完后, onFlowStepAdvance 自动加载下一个 stage 而不是收摊。
    bool             flow_real_sequential_  = false;
    int              flow_real_stage_idx_   = -1;   // 当前跑第几个 stage (0..8)
    // 实机串行模式可选 "跑到某个 stage 就停" 的上界 (含). -1 = 跑到底.
    // 取电按钮设 4, 换电按钮设 8, ▶ 开始走老路径设 -1.
    int              flow_real_end_stage_idx_ = -1;
    QVector<float>   flow_step_prev_joints_;       // last commanded joints, for dynamic step_ms calc
    // When the selected stage has a recorded script in stage_scripts_,
    // single-step mode walks THE SCRIPT (every type — MOVE_JOINTS,
    // GRIPPER, DWELL, etc.) instead of the hardcoded fine-states. The
    // fine-state walker is only the fallback for unrecorded stages.
    bool              flow_step_use_script_ = false;
    QVector<TaskStep> flow_step_script_;
    class QTimer   *step_advance_timer_ = nullptr; // 在子状态间推进的计时器
    QPushButton    *btn_flow_start_   = nullptr;
    QPushButton    *btn_flow_stop_    = nullptr;
    QPushButton    *btn_flow_reset_   = nullptr;
    QPushButton    *btn_flow_export_  = nullptr;   // 导出全部 9 stage 脚本到 JSON
    QPushButton    *btn_flow_import_  = nullptr;   // 加载 JSON 替换当前脚本
    QPushButton    *btn_flow_pickup_  = nullptr;   // 取电: phase1 (stage 1..5)
    QPushButton    *btn_flow_swap_    = nullptr;   // 换电: phase2 (stage 6..9)
    QLabel         *flow_status_label_= nullptr;
    QLabel         *config_lock_label_= nullptr;   // 🔒 已锁定 / 🔓 admin

    // ⚙ 配置权限. Starts LOCKED on every launch and is never persisted —
    // an app restart is meant to put the station back into operator mode.
    bool            config_unlocked_  = false;

    // Flow simulator (sim mode) — walks a precomputed playlist.
    //   - If a stage has a configured TaskStep script (stage_scripts_),
    //     each script step becomes one segment:
    //         MOVE_JOINTS → animate joints to target with speed_ratio
    //         GRIPPER/DWELL/AIRPORT_* → hold position for the step's
    //                                    natural duration
    //     This is what makes sim run with the operator's recorded points
    //     instead of the canned demo poses.
    //   - If a stage has NO script, fall back to the legacy fine-state
    //     walk: one segment per fine-state, target = demo_joints_deg.
    struct SimSegment {
        QString        stage_id;        // for marking stage finished + status
        QString        state_id;        // for setCurrentState; empty in script mode
        QString        label;           // status-bar text
        QVector<float> target_joints;   // empty = hold previous
        int            duration_ms = 1500;
        bool           is_script_step = false;
    };
    QTimer            *flow_sim_timer_      = nullptr;  // 30 Hz interp tick
    QVector<SimSegment> sim_playlist_;
    int                sim_seg_idx_         = -1;       // -1 = not running
    QVector<float>     flow_sim_start_joints_;
    qint64             flow_sim_step_start_ms_ = 0;
    int                flow_sim_step_dur_ms_   = 1500;

    // Real-mode polling of proc_battery_swap.
    QTimer        *swap_poll_timer_      = nullptr;
    bool           flow_running_         = false;
    bool           flow_simulating_      = false;

    // ── Per-stage user-configured TaskStep scripts (loaded from
    //    user home on construction, saved on dialog accept).
    //    scripts_[stage_id] = QVector<TaskStep>
    QHash<QString, QVector<TaskStep>> stage_scripts_;
    // Sim-mode script execution state (when current stage has a
    // configured script, we run its steps instead of the legacy
    // single-pose interpolation).
    int     flow_script_step_idx_  = -1;     // -1 = not in script mode
    qint64  flow_script_step_start_ms_ = 0;
    int     flow_script_step_dur_ms_   = 1500;
};

#endif // TAB4TASKCONFIG_H
