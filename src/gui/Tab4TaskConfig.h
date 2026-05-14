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
    void onFlowSimTick();             // 30 Hz interpolation when sim is running
    void onSwapStatusPoll();          // 2 Hz polling in real mode
    void onFlowStationClicked(QString state_id);
    void onStageConfigClicked(QString stage_id);   // ⚙ on TaskFlowWidget card

    // 单步调试模式: 流程图上选一张卡片(stage), "执行选中" 把该 stage 下
    // 所有 sub-state 依次通过 arm.move_joints 发到真机, 1.5 秒每一步.
    void onFlowStepExecute();
    void onFlowStepSkip();
    void onFlowStepAdvance();   // QTimer 触发, 把执行索引向后推

private:
    void buildUi();
    // Walks one TaskStep from the recorded script, dispatching to the
    // right RPC by step type and setting step_advance_timer_'s interval
    // to match the step's expected duration.
    void dispatchScriptStep(const struct TaskStep &step);
    // Poll airport.get_status; when all `watched_rails` report STALLED
    // (state == 2), stop step_advance_timer_ and advance the script step
    // immediately. `session_run_idx` captures flow_step_run_idx_ at
    // dispatch time so stale poll callbacks from a previous step don't
    // mis-advance a later one.
    void pollAirportStall(const QVector<int> &watched_rails,
                          const QString &label, int session_run_idx);
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
    QLabel         *flow_status_label_= nullptr;

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
