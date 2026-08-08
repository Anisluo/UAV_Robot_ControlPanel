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
    FIX_POINT       = 8,    // hold TCP at a target pose for duration_ms
                            // (定点跟踪 — arm goes to point, stays there)
    DOOR            = 9,    // 舱门 开/关/停 (proc_door, RS485 relay)
    HELIPAD         = 10,   // 停机坪升降 上升/下降/停 (same board)
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
// FIX_POINT        { x_mm, y_mm, z_mm, rx_deg, ry_deg, rz_deg,
//                    duration_ms: int }
//                  Drive the TCP to the recorded cartesian pose and hold
//                  it there for duration_ms. While "holding", the arm
//                  controller keeps the end-effector at the target —
//                  joints may rebalance/correct, but TCP point stays
//                  pinned. Useful for: "look at this point for N seconds".
// AIRPORT_GRIPPER  { open: bool }
// DOOR             { action: "open"|"close"|"stop", max_ms: int }
// HELIPAD          { action: "up"|"down"|"stop",    max_ms: int }
//                  Both drive proc_door over RS485. Motion there is
//                  asynchronous: the RPC returns as soon as the coils are
//                  energised and proc_door's own supervisor cuts power when
//                  the limit switch trips (X3/X4 for the hatch, X1/X2 for
//                  the lift) or its timeout expires. The orchestrator polls
//                  door.get_status and advances the moment the axis reports
//                  moving=false, so a travel that finishes early does not
//                  burn the rest of max_ms. max_ms is the GUI-side upper
//                  bound; "stop" is instantaneous and never waits.
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
