#ifndef TASKFLOWWIDGET_H
#define TASKFLOWWIDGET_H

#include <QWidget>
#include <QString>
#include <QVector>
#include <QHash>
#include <QTimer>

// TaskFlowWidget — HMI-style battery-swap flow chart.
//
// Renders 9 large "stage" cards on two rows (Phase 1 = 5 cards, Phase 2 = 4
// cards). Each card has a title bar with stage number + status indicator,
// plus 2–5 named signal rows underneath. Each signal row shows:
//
//     ● 标签:  实时值/状态
//
// where the dot color encodes the signal's state (pending / active / done /
// error) and the trailing text is whatever runtime value the caller pushes
// in via setSignal().
//
// Owner (Tab4TaskConfig) updates the widget either via:
//   - the legacy fine-grained API: setCurrentState / markFinished /
//     markError using the 24 old state IDs. These get mapped internally to
//     (stage, signal) pairs so existing sim code keeps working.
//   - the new direct API: setSignal(stage_id, signal_id, state, text) for
//     pushing real RPC values into individual signal rows.

struct TaskState {
    QString id;          // canonical fine-grained name, e.g. "MOVE_MONITOR"
    QString label;       // 4–6 char Chinese, shown only in tooltips now
    QString desc;        // long description
    int     phase;       // 1 or 2
    QVector<float> demo_joints_deg;  // pose hint for the sim runner
};

enum class SignalState : int {
    Pending = 0,    // grey — not executed yet
    Active  = 1,    // blue, pulsing — in progress
    Done    = 2,    // green — completed
    Error   = 3,    // red — fault
};

struct StageSignal {
    QString id;              // canonical name within stage, e.g. "approach"
    QString label;           // short Chinese, e.g. "接近到位"
    QString default_text;    // shown when state == Pending or Done w/o
                             // override, e.g. "—" or "完成"
};

struct TaskStage {
    QString id;              // "GRAB", "VISION", …
    QString title;           // Chinese title, e.g. "抓取电池"
    int     number;          // 1..9 — shown in title bar
    int     phase;           // 1 or 2 row
    QVector<StageSignal> entries;
};

class TaskFlowWidget : public QWidget {
    Q_OBJECT
public:
    explicit TaskFlowWidget(QWidget *parent = nullptr);

    // ── Static metadata ───────────────────────────────────────────────
    static const QVector<TaskStage>& stages();      // 9 cards
    static const QVector<TaskState>& states();      // 24 fine-grained
                                                    // (preserved for sim)

    // ── Legacy fine-grained API (back-compat) ─────────────────────────
    void setCurrentState(const QString &state_id);  // active + previous = done
    void markFinished(const QString &state_id);     // done
    void markError(const QString &state_id, const QString &err);
    void resetAll();
    QString currentState() const { return current_state_; }

    // ── New per-signal API ────────────────────────────────────────────
    // Push a runtime signal update straight into a card. `dyn_text` is the
    // string drawn after the colon (e.g. "运动中", "深度 285 mm",
    // "8.5 N"). Pass an empty string to fall back to the signal's
    // default_text.
    void setSignal(const QString &stage_id,
                   const QString &signal_id,
                   SignalState state,
                   const QString &dyn_text = QString());

    QSize sizeHint() const override { return {1100, 420}; }

signals:
    // Fires when the user clicks any signal row or stage card. state_id is
    // the OLD fine-grained name (matches legacy state catalogue), so the
    // sim runner can jump to that step.
    void stationClicked(QString state_id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onPulseTick();

private:
    struct SignalStatus {
        SignalState state = SignalState::Pending;
        QString     text;       // dynamic text after the colon
        QString     err_msg;    // populated when state == Error
    };

    // Cached card geometry per stage (recomputed on resize).
    struct StageGeom {
        QRectF card_rect;
        QVector<QRectF> signal_rects;  // one per signal in stage.signals
    };

    void recomputeGeometry();

    // Map an old state_id ("APPROACH_DRONE") to (stage_idx, signal_idx)
    // for back-compat APIs. Returns false if not found.
    bool resolveState(const QString &state_id,
                      int *out_stage_idx, int *out_signal_idx) const;

    // status_[stage_id][signal_id] = SignalStatus
    QHash<QString, QHash<QString, SignalStatus>> status_;
    QString  current_state_;       // last setCurrentState arg (legacy)

    QVector<StageGeom> geom_;      // index aligned with stages()
    QRectF             block1_rect_;   // 取电池 frame
    QRectF             block2_rect_;   // 装电池 frame
    int      hover_card_  = -1;
    int      hover_sig_   = -1;
    QTimer  *pulse_timer_ = nullptr;
    int      pulse_phase_ = 0;     // 0..359, animates Active glow
};

#endif // TASKFLOWWIDGET_H
