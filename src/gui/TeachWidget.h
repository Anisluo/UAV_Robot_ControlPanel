#ifndef TEACHWIDGET_H
#define TEACHWIDGET_H

#include <QGroupBox>
#include <QVector>
#include <QJsonObject>
#include <QDateTime>

class RpcClient;
class QPushButton;
class QListWidget;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QTimer;

// TeachWidget — record / edit / replay a sequence of arm waypoints.
//
// Workflow:
//   1. Drag the Piper to a position (lead-through teaching mode).
//   2. Click "📍 录点" to capture J1..J6 as one waypoint.
//   3. Per row, set gripper state (open / soft close / firm close) and
//      dwell time. Gripper *force* is stored per waypoint so the operator
//      can later raise it for actual grasp points without affecting
//      transit moves.
//   4. Click "💾 保存" to write the list as JSON. The file can be opened
//      in any text editor for fine-grained tuning (force %, dwell, etc.)
//   5. "📂 加载" reads it back, "▶ 回放" plays the sequence over RPC.
//
// JSON schema:
//   {
//     "name": "battery_swap_v1",
//     "captured": "ISO-8601",
//     "default_step_ms": 1500,
//     "waypoints": [
//       {"label": "wp1", "joints": [j1..j6], "gripper_angle": 60,
//        "gripper_force_pct": 30, "dwell_ms": 0},
//       ...
//     ]
//   }
class TeachWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit TeachWidget(RpcClient *rpc, QWidget *parent = nullptr);

    enum GripperState {
        GripperUnchanged = 0,   // do not touch gripper at this point
        GripperOpen      = 1,   // 60°, low force
        GripperCloseSoft = 2,   // 5°,  ~30% force (for travel / non-grip)
        GripperCloseFirm = 3,   // 5°,  ~90% force (for real grasp)
    };

private slots:
    void onRecordPoint();
    void onDeleteSelected();
    void onClearAll();
    void onSaveToFile();
    void onLoadFromFile();
    void onStartReplay();
    void onStopReplay();
    void onReplayTick();
    void onRowDoubleClicked(int row);

private:
    void buildUi();
    void rebuildList();
    void appendLog(const QString &msg);
    void executeStep(int idx);
    void startReplayConfirmed();    // continuation after pre-flight ctrl_mode check

    RpcClient *rpc_;

    struct Waypoint {
        QString        label;
        QString        note;                    // free-form operator memo
        QVector<float> joints;                  // J1..J6 in degrees
        GripperState   gripper_state = GripperUnchanged;
        int            gripper_angle    = 60;   // ° (5=closed, 60=open)
        int            gripper_force_pct = 30;  // 0-100
        int            dwell_ms = 0;            // pause after motion arrives
    };
    QVector<Waypoint> waypoints_;

    QPushButton    *btn_record_   = nullptr;
    QPushButton    *btn_delete_   = nullptr;
    QPushButton    *btn_clear_    = nullptr;
    QPushButton    *btn_save_     = nullptr;
    QPushButton    *btn_load_     = nullptr;
    QPushButton    *btn_replay_   = nullptr;
    QPushButton    *btn_stop_     = nullptr;
    QSpinBox       *step_ms_spin_ = nullptr;   // ms per waypoint (interp)
    QListWidget    *wp_list_      = nullptr;
    QLabel         *count_label_  = nullptr;
    QPlainTextEdit *log_view_     = nullptr;

    // Replay state machine ticks at 50 ms.
    QTimer *replay_timer_  = nullptr;
    int     replay_idx_    = -1;
    qint64  replay_start_ms_ = 0;
    int     replay_step_dur_ms_ = 1500;
};

#endif // TEACHWIDGET_H
