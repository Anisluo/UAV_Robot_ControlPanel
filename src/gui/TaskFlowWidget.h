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

    // 单步模式选中: 高亮"整个卡片(stage)", 而不是单条信号. 传入 state_id
    // 会自动解析出它所属的 stage 并高亮; 传入 "" 清空选中.
    void setSelectedState(const QString &state_id);
    void setSelectedStage(const QString &stage_id);
    QString selectedState() const { return selected_state_; }
    QString selectedStage() const { return selected_stage_; }

    // 工具方法: 给一个 legacy state_id ("GRAB_LIGHT"), 返回它的 stage_id
    // ("GRAB"). 找不到返回空字符串.
    static QString stageOfState(const QString &state_id);
    // 工具方法: 给一个 stage_id, 返回该 stage 下所有 legacy state_id, 顺序
    // 跟 states() 表里保持一致(也就是执行顺序).
    static QVector<QString> statesInStage(const QString &stage_id);

    // 默认 false: 整个流程图是只读显示, 鼠标点击什么都不做(避免误操作).
    // Tab4 切到"单步"模式时把它开成 true, 退出单步再关回 false.
    void setClickEnabled(bool enabled);
    bool isClickEnabled() const { return click_enabled_; }

    // 默认 false: ⚙ 配置按钮锁定 — 画成挂锁, 点击只发 configLocked().
    // Tab4 在操作员通过 AuthDialog 登录后设成 true. Locked is the state the
    // app STARTS in; nothing here persists it across runs.
    void setConfigUnlocked(bool unlocked);
    bool isConfigUnlocked() const { return config_unlocked_; }

    QSize sizeHint() const override { return {1100, 420}; }

signals:
    // Fires when the user clicks any signal row or stage card. state_id is
    // the OLD fine-grained name (matches legacy state catalogue), so the
    // sim runner can jump to that step.
    void stationClicked(QString state_id);

    // Fires when the operator clicks the small ⚙ gear icon in a stage
    // card's top-right corner. stage_id matches TaskStage::id. Tab4
    // catches this to open StageConfigDialog for that stage.
    // Only emitted while config is unlocked — see configLocked().
    void stageConfigClicked(QString stage_id);

    // Fires instead of stageConfigClicked() when the gear is clicked while
    // locked. Tab4 turns this into a "已锁定" notice — a dead button that
    // gives no feedback at all reads as a bug, so the click still says
    // something. It says only that the button is locked: the unlock chord
    // is never surfaced anywhere in the UI or the log.
    void configLocked();

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
        QRectF gear_rect;              // hit box for the ⚙ button in title bar
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
    QString  selected_state_;      // (legacy, 兼容; 当前等于 selected_stage_)
    QString  selected_stage_;      // 单步模式选中的整个卡片 stage_id, 空 = 无
    bool     click_enabled_ = false;
    bool     config_unlocked_ = false;   // ⚙ gated behind AuthDialog

    QVector<StageGeom> geom_;      // index aligned with stages()
    QRectF             block1_rect_;   // 取电池 frame
    QRectF             block2_rect_;   // 装电池 frame
    int      hover_card_  = -1;
    int      hover_sig_   = -1;
    int      hover_gear_  = -1;       // card index whose ⚙ is hovered, -1 = none
    QTimer  *pulse_timer_ = nullptr;
    int      pulse_phase_ = 0;     // 0..359, animates Active glow
};

#endif // TASKFLOWWIDGET_H
