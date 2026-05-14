#ifndef TASKSTEP_H
#define TASKSTEP_H

#include <QString>
#include <QVector>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>

// One configurable action that the operator records into a stage of the
// battery-swap pipeline. The 9 TaskFlowWidget stages each own an ordered
// list of these; when the task runs (simulation OR real backend) the
// orchestrator walks the list and dispatches per-type.
enum class StepType {
    MOVE_JOINTS     = 0,    // arm 6 joint angles
    MOVE_CARTESIAN  = 1,    // arm end pose (X/Y/Z mm + RX/RY/RZ deg)
    GRIPPER         = 2,    // piper gripper angle_mm + force_pct
    AIRPORT_RAIL    = 3,    // platform rails 1+3 lock / release; stall-driven
    AIRPORT_GRIPPER = 4,    // platform UAV-lock gripper open/close
    WAIT_DETECT_UAV = 5,    // poll NPU, wait for class=4 present/absent
    WAIT_DETECT_BAT = 6,    // poll battery_tracker, wait for class=200
    DWELL           = 7,    // pure delay (ms)
};

struct TaskStep {
    StepType     type = StepType::DWELL;
    QString      label;        // optional operator note shown in list
    QVariantMap  params;       // type-specific (see schema below)

    QJsonObject toJson() const;
    static TaskStep fromJson(const QJsonObject &o);
    // 简短描述, 在 dialog/log 列表里展示给操作员看 (e.g. "J1=10 J2=30 ...")
    QString summary() const;
    static QString typeLabel(StepType t);
};

// ── Param schemas (by type) ───────────────────────────────────────────
//
// MOVE_JOINTS      { joints: [j1..j6], speed_ratio: 0.0-1.0 }
//                  (joint angles in degrees; speed_ratio default 1.0)
// MOVE_CARTESIAN   { x_mm, y_mm, z_mm, rx_deg, ry_deg, rz_deg,
//                    mode: "P" | "L"  }
// GRIPPER          { angle_mm: 0..70, force_pct: 0..100 }
// AIRPORT_RAIL     { action: "lock"|"release"|"rail2_fwd"|"rail2_back",
//                    stop_mode: "stall"|"distance" (default "stall"),
//                    speed_rpm: int,
//                    distance_mm: float (only used when stop_mode=distance),
//                    max_ms: int }
//                  stall mode:
//                    lock/release → airport.lock / .release (pair clamp).
//                    rail2_fwd/back → airport.set_speed (rail=1, ±rpm).
//                    Backend stall monitor cuts the motor on jam.
//                  distance mode:
//                    Calls airport.move_distance (open-loop position cmd)
//                    one or two rails. Motor self-stops on its pulse
//                    counter. Per-rail completion monitor flips state
//                    IDLE on reach / STALLED on early collision.
//                  max_ms = GUI-side safety upper bound for both modes.
// AIRPORT_GRIPPER  { open: bool }
// WAIT_DETECT_UAV  { present: bool, timeout_ms: int }
//                  (present=true → wait until detected; false → wait until gone)
// WAIT_DETECT_BAT  { present: bool, timeout_ms: int }
// DWELL            { ms: int }


// One stage script = ordered list of TaskSteps.
struct StageScript {
    QString          stage_id;        // matches TaskFlowWidget::TaskStage::id
    QVector<TaskStep> steps;

    QJsonObject toJson() const;
    static StageScript fromJson(const QJsonObject &o);
};

// All-9 task-script bundle, persisted as a single JSON file in user home.
struct TaskConfig {
    int                            version = 1;
    QHash<QString, QVector<TaskStep>> scripts;   // stage_id → step list

    QJsonObject toJson() const;
    static TaskConfig fromJson(const QJsonObject &o);

    bool saveToHomeFile() const;             // writes <user_home>/.../task_stages.json
    static TaskConfig loadFromHomeFile();    // reads same; returns empty on error
    static QString homeFilePath();
};

#endif // TASKSTEP_H
