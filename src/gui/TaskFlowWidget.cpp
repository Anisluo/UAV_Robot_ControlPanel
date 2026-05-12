#include "TaskFlowWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QFontMetricsF>
#include <QtMath>
#include <QToolTip>

// ════════════════════════════════════════════════════════════════════════
// Static data — 9 stages + 24 fine-grained legacy states + mapping table
// ════════════════════════════════════════════════════════════════════════

const QVector<TaskStage>& TaskFlowWidget::stages() {
    static const QVector<TaskStage> kStages = {
        // ── Phase 1 ──────────────────────────────────────────────────
        {"INIT", "初始化",     1, 1, {
            {"home_done",      "关节归零",      "—"},
            {"monitor_ready",  "监视位姿",      "—"},
        }},
        {"VISION", "视觉定位", 2, 1, {
            {"uav_centered",   "UAV 搜索",     "—"},
            {"uav_static",     "UAV 定位",     "—"},
        }},
        {"LOCK", "机场锁定",   3, 1, {
            {"uav_landed",     "降落到位",      "—"},
            {"airport_jaws",   "左右夹爪",      "—"},
            {"lock_done",      "锁紧到位",      "—"},
        }},
        {"GRAB", "抓取电池",   4, 1, {
            {"approach",       "接近到位",      "—"},
            {"light_grip",     "轻夹电池",      "—"},
            {"extract",        "抽出电池",      "—"},
            {"firm_grip",      "重夹大力",      "—"},
            {"airport_free",   "机场松开",      "—"},
        }},
        {"PLATFORM", "平台暂存", 5, 1, {
            {"to_platform",    "搬到平台",      "—"},
            {"head_aligned",   "调头夹住",      "—"},
            {"insert_slot",    "插入电池槽",    "—"},
            {"slot_released",  "夹爪松开",      "—"},
        }},
        // ── Phase 2 ──────────────────────────────────────────────────
        {"FETCH", "取新电池",  6, 2, {
            {"new_grabbed",    "从槽取出",      "—"},
            {"on_platform",    "放回平台",      "—"},
            {"tail_grip",      "夹住尾部",      "—"},
        }},
        {"HANDOFF", "送交机场", 7, 2, {
            {"at_handoff",     "送到交接位",    "—"},
            {"airport_catch",  "机场接住",      "—"},
            {"arm_release",    "机械臂松开",    "—"},
        }},
        {"INSERT", "装入无人机", 8, 2, {
            {"push_in",        "推入 UAV",     "—"},
            {"seated",         "电池就位",      "—"},
        }},
        {"DONE", "完成",       9, 2, {
            {"wait_takeoff",   "等待起飞",      "—"},
            {"task_done",      "任务完成",      "—"},
        }},
    };
    return kStages;
}

const QVector<TaskState>& TaskFlowWidget::states() {
    // Simulation script (driven by Tab4TaskConfig::onFlowSimTick at 30 Hz):
    //
    //   ① 松开夹爪, 抬到监视位                — HOME / MOVE_MONITOR
    //   ② 平台 A 上方下降, 夹起平躺的电池      — CENTERING ... GRAB_LIGHT
    //   ③ 闭合夹爪, 抬起                       — EXTRACT / REGRIP_FIRM / AIRPORT_RELEASE
    //   ④ 搬到平台 B 另一位置                  — MOVE_TO_PLATFORM / ADJUST_HEAD
    //   ⑤ 松开夹爪, 放回平台 B                — INSERT_SLOT / RELEASE_IN_SLOT
    //   ⑥ J6 翻 90°, 竖着夹起电池"大头"       — FETCH_NEW_BAT ... MOVE_HANDOFF
    //   ⑦ 移到电池槽上方, 直线插入             — AIRPORT_LOCK_NEW ... AIRPORT_INSERT
    //   ⑧ 槽内松开, 退回 home                  — WAIT_TAKEOFF / DONE
    //
    // J6 双重用途: ≈0 = 夹爪横向 + 张开, ≈30 = 横向闭合 (轻夹用), ≈90 = 竖向
    // 夹取大头. 仿真器只看关节插值, 没有独立"夹爪宽度" 通道, 所以用 J6 的
    // 旋转角变化作为"开/合 / 横竖切换"的视觉提示, 看着像真在动夹爪.
    static const QVector<TaskState> kStates = {
        // ① 松开夹爪, 抬到监视位
        {"HOME",            "归零",        "上电归零, 全部关节 0° (夹爪默认张开)", 1, {  0,   0,    0,   0,   0,   0}},
        {"MOVE_MONITOR",    "监视位",      "抬手到平台监视姿态, 夹爪保持张开",       1, {  0,  30,  -45,   0,  15,   0}},

        // ② 平台 A 上方下降, 准备夹起平躺电池
        {"CENTERING",       "视觉居中",    "转向平台 A, 在电池正上方居中",          1, {-30,  30,  -45,   0,  15,   0}},
        {"DRONE_STATIC",    "等待静止",    "悬停, 等待视觉确认目标静止",            1, {-30,  50,  -65,   0,  25,   0}},
        {"AIRPORT_LOCK",    "机场夹紧",    "下沉到平台 A 高度, 张开等待接触",        1, {-30,  70,  -80,   0,  30,   0}},
        {"APPROACH_DRONE",  "接近电池",    "MoveL 直线接触电池, 夹爪仍张开",         1, {-30,  70,  -80,   0,  30,   0}},

        // ③ 闭合夹爪, 抬起带电池
        {"GRAB_LIGHT",      "轻夹电池",    "夹爪轻力闭合 (J6 ≈ 30° 视觉闭合)",     1, {-30,  70,  -80,   0,  30,  30}},
        {"EXTRACT",         "抽出电池",    "MoveL 反向直线抬起电池",                1, {-30,  50,  -60,   0,  20,  30}},
        {"REGRIP_FIRM",     "重夹大力",    "短暂悬停, 重夹更稳",                    1, {-30,  50,  -60,   0,  20,  30}},
        {"AIRPORT_RELEASE", "机场松开",    "再抬高, 切换到搬运高度",                1, {-30,  40,  -50,   0,  15,  30}},

        // ④ 搬到平台 B
        {"MOVE_TO_PLATFORM","搬到平台",    "MoveJ 绕底座转向平台 B",                1, { 40,  40,  -50,   0,  15,  30}},
        {"ADJUST_HEAD",     "调头夹住",    "下降到平台 B 上方, 对位释放点",          1, { 40,  60,  -70,   0,  25,  30}},

        // ⑤ 松开夹爪, 放回平台 B
        {"INSERT_SLOT",     "插入电池槽",  "夹爪张开释放电池 (J6 回到 0°)",         1, { 40,  60,  -70,   0,  25,   0}},
        {"RELEASE_IN_SLOT", "槽内松开",    "抬高离开放置点",                        1, { 40,  40,  -50,   0,  15,   0}},

        // ⑥ J6 翻 90°, 竖着夹起电池"大头"
        {"FETCH_NEW_BAT",   "取新电池",    "夹爪翻转 90° 竖向, 下降到电池大头位置", 2, { 40,  60,  -70,   0,  25,  90}},
        {"PLACE_PLATFORM",  "放回平台",    "竖向闭合 (J6 维持 90°, 视觉锁定)",      2, { 40,  60,  -70,   0,  25,  90}},
        {"REGRIP_TAIL",     "夹住尾部",    "确认夹紧, 短暂悬停",                    2, { 40,  60,  -70,   0,  25,  90}},
        {"MOVE_HANDOFF",    "送交接位",    "竖向托起电池, 转向电池槽方向",          2, { 10,  40,  -50,   0,  15,  90}},

        // ⑦ 移到电池槽上方, 直线插入
        {"AIRPORT_LOCK_NEW","机场接住",    "MoveJ 摆到电池槽正上方, 维持竖向",      2, {-60,  40,  -50,   0,  15,  90}},
        {"ARM_RELEASE",     "机械臂松开",  "下降到电池槽口",                        2, {-60,  60,  -70,   0,  25,  90}},
        {"AIRPORT_INSERT",  "推入无人机",  "MoveL 直线插到位",                      2, {-60,  75,  -85,   0,  35,  90}},

        // ⑧ 槽内松开, 退回 home
        {"WAIT_TAKEOFF",    "等待起飞",    "在槽内松开 (J6 → 0°), 等系统确认",     2, {-60,  75,  -85,   0,  35,   0}},
        {"DONE",            "完成",        "退回原点, 任务完成",                    2, {  0,   0,    0,   0,   0,   0}},
    };
    return kStates;
}

namespace {

// Old fine-grained state id → (stage_id, signal_id). Ordering inside this
// table also defines progression: when state X becomes Active, everything
// before X in the table auto-marks Done.
struct LegacyMap { const char *state; const char *stage; const char *signal; };
static const QVector<LegacyMap> &legacyMap() {
    static const QVector<LegacyMap> kMap = {
        {"HOME",             "INIT",     "home_done"},
        {"MOVE_MONITOR",     "INIT",     "monitor_ready"},
        {"CENTERING",        "VISION",   "uav_centered"},
        {"DRONE_STATIC",     "VISION",   "uav_static"},
        {"AIRPORT_LOCK",     "LOCK",     "lock_done"},
        {"APPROACH_DRONE",   "GRAB",     "approach"},
        {"GRAB_LIGHT",       "GRAB",     "light_grip"},
        {"EXTRACT",          "GRAB",     "extract"},
        {"REGRIP_FIRM",      "GRAB",     "firm_grip"},
        {"AIRPORT_RELEASE",  "GRAB",     "airport_free"},
        {"MOVE_TO_PLATFORM", "PLATFORM", "to_platform"},
        {"ADJUST_HEAD",      "PLATFORM", "head_aligned"},
        {"INSERT_SLOT",      "PLATFORM", "insert_slot"},
        {"RELEASE_IN_SLOT",  "PLATFORM", "slot_released"},
        {"FETCH_NEW_BAT",    "FETCH",    "new_grabbed"},
        {"PLACE_PLATFORM",   "FETCH",    "on_platform"},
        {"REGRIP_TAIL",      "FETCH",    "tail_grip"},
        {"MOVE_HANDOFF",     "HANDOFF",  "at_handoff"},
        {"AIRPORT_LOCK_NEW", "HANDOFF",  "airport_catch"},
        {"ARM_RELEASE",      "HANDOFF",  "arm_release"},
        {"AIRPORT_INSERT",   "INSERT",   "push_in"},
        {"WAIT_TAKEOFF",     "DONE",     "wait_takeoff"},
        {"DONE",             "DONE",     "task_done"},
    };
    return kMap;
}

// Visual constants ──────────────────────────────────────────────────────
constexpr int kMargin        = 12;
constexpr int kCardW         = 190;
constexpr int kTitleH        = 28;
constexpr int kSignalH       = 22;
constexpr int kCardPad       = 6;
constexpr int kCardHGap      = 16;       // horizontal gap inside a block
constexpr int kCardVGap      = 36;       // vertical gap between rows inside a block
constexpr int kBlockPad      = 16;       // padding inside each phase block
constexpr int kBlockGap      = 50;       // gap between block-1 and block-2
constexpr int kBlockHeaderH  = 28;
constexpr int kDotR          = 5;
constexpr int kPhase1Cols    = 2;        // 取电池 layout: 2-col grid (2 + 2 + 1)
constexpr int kPhase2Cols    = 2;        // 装电池 layout: 2-col grid (2 + 2)

// Draw a Bézier-curve arrow from `start` to `end`. The curve's control
// points are nudged horizontally so adjacent same-row cards get a near-
// straight line, while row-break / cross-block jumps get a soft S-curve.
// `style_dashed` flips between solid and dashed (used for the cross-block
// hop). An arrowhead is rendered at the endpoint tangent to the curve.
void drawBezierArrow(QPainter &p, QPointF start, QPointF end,
                     QColor col, bool style_dashed = false,
                     bool vertical = false) {
    const qreal dx = end.x() - start.x();
    const qreal dy = end.y() - start.y();
    const qreal absdx = std::abs(dx);
    const qreal absdy = std::abs(dy);
    QPointF c1, c2;
    if (vertical) {
        // Caller has chosen vertical exit/entry — push controls along Y so
        // the arrow leaves the bottom edge straight down then curves into
        // the top edge of the next card.
        const qreal sy = (dy >= 0 ? 1 : -1) *
                         std::max<qreal>(std::abs(dy) * 0.6, 30.0);
        c1 = QPointF(start.x(), start.y() + sy);
        c2 = QPointF(end.x(),   end.y()   - sy);
    } else if (absdy < 8 && absdx > 0) {
        // ~horizontal hop, mild belly so it doesn't look like a ruler
        c1 = QPointF(start.x() + dx * 0.35, start.y());
        c2 = QPointF(start.x() + dx * 0.65, end.y());
    } else if (absdx < 8) {
        // ~vertical hop
        c1 = QPointF(start.x(),               start.y() + dy * 0.35);
        c2 = QPointF(end.x(),                 start.y() + dy * 0.65);
    } else {
        // Diagonal / row-break — push controls strongly along the direction
        // of the bigger dimension to keep the curve smooth.
        const qreal sx = (dx > 0 ? 1 : -1) * std::max<qreal>(absdx * 0.55, 35.0);
        c1 = QPointF(start.x() + sx, start.y());
        c2 = QPointF(end.x()   - sx, end.y());
    }

    QPainterPath path;
    path.moveTo(start);
    path.cubicTo(c1, c2, end);

    QPen pen(col);
    pen.setWidth(2);
    pen.setCapStyle(Qt::RoundCap);
    if (style_dashed) {
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({6, 5});
    }
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Arrowhead: tangent direction from c2 → end
    const QPointF d = end - c2;
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 1e-3) return;
    const qreal ux = d.x() / len, uy = d.y() / len;
    constexpr qreal head = 9.0;
    constexpr qreal wing = 0.45;     // radians, ≈26°
    const QPointF a1(end.x() - head * (ux * std::cos(wing) - uy * std::sin(wing)),
                     end.y() - head * (uy * std::cos(wing) + ux * std::sin(wing)));
    const QPointF a2(end.x() - head * (ux * std::cos(-wing) - uy * std::sin(-wing)),
                     end.y() - head * (uy * std::cos(-wing) + ux * std::sin(-wing)));
    QPolygonF tri; tri << end << a1 << a2;
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPolygon(tri);
}

QColor stateColor(SignalState s, int pulse_phase) {
    switch (s) {
        case SignalState::Pending: return QColor(0x6a, 0x71, 0x80);
        case SignalState::Done:    return QColor(0x45, 0xcc, 0x7a);
        case SignalState::Error:   return QColor(0xff, 0x52, 0x52);
        case SignalState::Active: {
            float t = 0.5f + 0.5f * std::cos(pulse_phase * float(M_PI) / 180.0f);
            int  r = int(0x36 + (0x6a - 0x36) * t);
            int  g = int(0x88 + (0xc8 - 0x88) * t);
            int  b = int(0xee + (0xff - 0xee) * t);
            return QColor(r, g, b);
        }
    }
    return Qt::gray;
}

} // namespace

// ════════════════════════════════════════════════════════════════════════

TaskFlowWidget::TaskFlowWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(900, 360);

    pulse_timer_ = new QTimer(this);
    pulse_timer_->setInterval(40);
    connect(pulse_timer_, &QTimer::timeout, this, &TaskFlowWidget::onPulseTick);
    pulse_timer_->start();

    resetAll();
}

bool TaskFlowWidget::resolveState(const QString &state_id,
                                   int *out_stage_idx, int *out_signal_idx) const {
    for (const auto &m : legacyMap()) {
        if (state_id == QLatin1String(m.state)) {
            const auto &sl = stages();
            for (int si = 0; si < sl.size(); ++si) {
                if (sl[si].id == QLatin1String(m.stage)) {
                    for (int xi = 0; xi < sl[si].entries.size(); ++xi) {
                        if (sl[si].entries[xi].id == QLatin1String(m.signal)) {
                            if (out_stage_idx)  *out_stage_idx  = si;
                            if (out_signal_idx) *out_signal_idx = xi;
                            return true;
                        }
                    }
                }
            }
            return false;
        }
    }
    return false;
}

void TaskFlowWidget::setCurrentState(const QString &state_id) {
    int si = -1, xi = -1;
    if (!resolveState(state_id, &si, &xi)) return;
    current_state_ = state_id;

    const auto &sl = stages();
    for (int s = 0; s < sl.size(); ++s) {
        for (int x = 0; x < sl[s].entries.size(); ++x) {
            const bool before = (s < si) || (s == si && x < xi);
            const bool here   = (s == si && x == xi);
            const QString &stid = sl[s].id;
            const QString &sgid = sl[s].entries[x].id;
            auto &st = status_[stid][sgid];
            if (here) {
                st.state = SignalState::Active;
                if (st.text.isEmpty()) st.text = QStringLiteral("进行中");
            } else if (before) {
                if (st.state != SignalState::Done && st.state != SignalState::Error) {
                    st.state = SignalState::Done;
                    if (st.text.isEmpty()) st.text = QStringLiteral("完成");
                }
            } else {
                if (st.state == SignalState::Active) {
                    st.state = SignalState::Pending;
                    st.text.clear();
                }
            }
        }
    }
    update();
}

void TaskFlowWidget::markFinished(const QString &state_id) {
    int si = -1, xi = -1;
    if (!resolveState(state_id, &si, &xi)) return;
    const auto &sl = stages();
    auto &st = status_[sl[si].id][sl[si].entries[xi].id];
    st.state = SignalState::Done;
    if (st.text.isEmpty() || st.text == QStringLiteral("进行中"))
        st.text = QStringLiteral("完成");
    update();
}

void TaskFlowWidget::markError(const QString &state_id, const QString &err) {
    int si = -1, xi = -1;
    if (!resolveState(state_id, &si, &xi)) return;
    const auto &sl = stages();
    auto &st = status_[sl[si].id][sl[si].entries[xi].id];
    st.state = SignalState::Error;
    st.err_msg = err;
    st.text = err.isEmpty() ? QStringLiteral("故障") : err;
    update();
}

void TaskFlowWidget::resetAll() {
    status_.clear();
    current_state_.clear();
    selected_state_.clear();
    for (const auto &stage : stages()) {
        for (const auto &sig : stage.entries) {
            status_[stage.id][sig.id] = SignalStatus{SignalState::Pending, QString(), QString()};
        }
    }
    update();
}

void TaskFlowWidget::setSelectedState(const QString &state_id) {
    // 把传入的 state_id 解析成它所属的 stage_id,然后整个卡片高亮.
    selected_state_ = state_id;
    int s_idx = -1, k_idx = -1;
    QString new_stage;
    if (!state_id.isEmpty() && resolveState(state_id, &s_idx, &k_idx) && s_idx >= 0) {
        new_stage = stages()[s_idx].id;
    }
    if (selected_stage_ == new_stage) return;
    selected_stage_ = new_stage;
    update();
}

void TaskFlowWidget::setSelectedStage(const QString &stage_id) {
    if (selected_stage_ == stage_id) return;
    selected_stage_ = stage_id;
    selected_state_.clear();
    update();
}

QString TaskFlowWidget::stageOfState(const QString &state_id) {
    for (const auto &m : legacyMap()) {
        if (state_id == QLatin1String(m.state)) {
            return QString::fromLatin1(m.stage);
        }
    }
    return QString();
}

QVector<QString> TaskFlowWidget::statesInStage(const QString &stage_id) {
    QVector<QString> out;
    for (const auto &m : legacyMap()) {
        if (stage_id == QLatin1String(m.stage)) {
            out.append(QString::fromLatin1(m.state));
        }
    }
    return out;
}

void TaskFlowWidget::setSignal(const QString &stage_id,
                                const QString &signal_id,
                                SignalState state,
                                const QString &dyn_text) {
    auto &m = status_[stage_id];
    auto &st = m[signal_id];
    st.state = state;
    st.text = dyn_text;
    if (state == SignalState::Error) st.err_msg = dyn_text;
    update();
}

// ────────────────────────────────────────────────────────────────────────
// Layout
// ────────────────────────────────────────────────────────────────────────

void TaskFlowWidget::recomputeGeometry() {
    geom_.clear();
    const auto &sl = stages();
    geom_.resize(sl.size());

    // Split cards into the two blocks (phase 1 vs phase 2) preserving order.
    QVector<int> p1_indices, p2_indices;
    for (int i = 0; i < sl.size(); ++i)
        (sl[i].phase == 1 ? p1_indices : p2_indices).append(i);

    auto cardH = [&](int idx) {
        return kTitleH + kCardPad + int(sl[idx].entries.size()) * kSignalH + kCardPad;
    };

    // For each block, choose row counts based on # cards + column count.
    auto rowOf = [](int local_idx, int cols) { return local_idx / cols; };
    auto colOf = [](int local_idx, int cols) { return local_idx % cols; };
    const int p1_rows = (p1_indices.size() + kPhase1Cols - 1) / kPhase1Cols;
    const int p2_rows = (p2_indices.size() + kPhase2Cols - 1) / kPhase2Cols;

    // Row max-height (so cards in same row align vertically and aren't
    // jagged when one stage has more signal rows than its neighbour).
    auto rowMaxH = [&](const QVector<int> &block, int cols, int row) {
        int h = 0;
        for (int li = 0; li < block.size(); ++li) {
            if (rowOf(li, cols) != row) continue;
            h = std::max(h, cardH(block[li]));
        }
        return h;
    };

    auto blockSize = [&](const QVector<int> &block, int cols, int rows) {
        const int w = kBlockPad * 2 + cols * kCardW + (cols - 1) * kCardHGap;
        int h = kBlockPad * 2 + kBlockHeaderH;
        for (int r = 0; r < rows; ++r) {
            h += rowMaxH(block, cols, r);
            if (r + 1 < rows) h += kCardVGap;
        }
        return QSize(w, h);
    };

    const QSize b1_sz = blockSize(p1_indices, kPhase1Cols, p1_rows);
    const QSize b2_sz = blockSize(p2_indices, kPhase2Cols, p2_rows);

    // Centre the two blocks horizontally as a pair (keep block widths
    // intact; centre the gap-spanning pair inside the widget).
    const int total_w = b1_sz.width() + kBlockGap + b2_sz.width();
    const int pair_left = std::max(kMargin, (width() - total_w) / 2);
    const int top = kMargin;

    block1_rect_ = QRectF(pair_left, top, b1_sz.width(),
                           std::max(b1_sz.height(), b2_sz.height()));
    block2_rect_ = QRectF(pair_left + b1_sz.width() + kBlockGap, top,
                           b2_sz.width(),
                           std::max(b1_sz.height(), b2_sz.height()));

    // Lay out cards inside each block.
    auto layoutBlock = [&](const QVector<int> &block, int cols, int rows,
                            const QRectF &area) {
        const int inner_left = int(area.left()) + kBlockPad;
        const int inner_top  = int(area.top())  + kBlockPad + kBlockHeaderH;
        for (int r = 0; r < rows; ++r) {
            // y baseline for this row
            int row_y = inner_top;
            for (int rr = 0; rr < r; ++rr)
                row_y += rowMaxH(block, cols, rr) + kCardVGap;
            const int row_h = rowMaxH(block, cols, r);
            // Determine cards in this row
            QVector<int> row_local;
            for (int li = 0; li < block.size(); ++li)
                if (rowOf(li, cols) == r) row_local.append(li);
            // Centre row horizontally if it's not full
            const int row_cards_w = row_local.size() * kCardW
                                   + (row_local.size() - 1) * kCardHGap;
            const int row_inner_w = cols * kCardW + (cols - 1) * kCardHGap;
            const int row_offset  = std::max(0, (row_inner_w - row_cards_w) / 2);
            for (int li : row_local) {
                const int idx = block[li];
                const int col = colOf(li, cols);
                const int h = cardH(idx);
                // Top-align to row baseline (so dots line up at the top)
                const int x = inner_left + row_offset + col * (kCardW + kCardHGap);
                const int y = row_y;
                (void)row_h;
                StageGeom g;
                g.card_rect = QRectF(x, y, kCardW, h);
                g.signal_rects.resize(sl[idx].entries.size());
                for (int k = 0; k < sl[idx].entries.size(); ++k) {
                    const qreal sy = y + kTitleH + kCardPad + k * kSignalH;
                    g.signal_rects[k] = QRectF(x + 6, sy, kCardW - 12, kSignalH);
                }
                geom_[idx] = g;
            }
        }
    };

    layoutBlock(p1_indices, kPhase1Cols, p1_rows, block1_rect_);
    layoutBlock(p2_indices, kPhase2Cols, p2_rows, block2_rect_);
}

void TaskFlowWidget::resizeEvent(QResizeEvent *) {
    recomputeGeometry();
}

// ────────────────────────────────────────────────────────────────────────
// Paint
// ────────────────────────────────────────────────────────────────────────

void TaskFlowWidget::paintEvent(QPaintEvent *) {
    if (geom_.isEmpty()) recomputeGeometry();
    const auto &sl = stages();

    // (selected_state_ + selected_signal_idx 残留变量已废弃 — 现在按 stage 高亮)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x14, 0x18, 0x22));

    QFont f = p.font();
    f.setFamily("Microsoft YaHei");

    // ── Two block frames + headers ────────────────────────────────────
    auto drawBlock = [&](const QRectF &r, const QString &title, const QColor &accent) {
        if (r.isNull()) return;
        p.setBrush(QColor(0x18, 0x1e, 0x2c, 200));
        QPen border(QColor(accent.red(), accent.green(), accent.blue(), 160));
        border.setWidth(2);
        p.setPen(border);
        p.drawRoundedRect(r, 12, 12);
        // Title bar across the top
        QRectF tb(r.left() + 1, r.top() + 1, r.width() - 2, kBlockHeaderH);
        QLinearGradient g(tb.topLeft(), tb.bottomLeft());
        g.setColorAt(0, QColor(accent.red(), accent.green(), accent.blue(), 60));
        g.setColorAt(1, QColor(0x18, 0x1e, 0x2c, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        QPainterPath tp;
        tp.addRoundedRect(tb, 12, 12);
        tp.addRect(QRectF(tb.left(), tb.bottom() - 10, tb.width(), 10));
        p.drawPath(tp);
        f.setPointSize(11); f.setBold(true); p.setFont(f);
        p.setPen(accent);
        p.drawText(tb.adjusted(14, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, title);
        f.setBold(false);
    };
    drawBlock(block1_rect_, QStringLiteral("◆  取电池  (Phase 1)"),
              QColor(0x6e, 0xb8, 0xff));
    drawBlock(block2_rect_, QStringLiteral("◆  装电池  (Phase 2)"),
              QColor(0xff, 0xb0, 0x6e));

    // ── Bezier arrows between consecutive cards within each phase ────
    //
    // Within a grid layout each card has up to two anchored connectors:
    //   • horizontal: right-edge → left-edge (same row)
    //   • vertical:   bottom-edge → top-edge (row-wrap)
    // Picking distinct exit/entry points per direction stops two arrows
    // from piling onto the same side of a card.
    const QColor kArrowCol(0x6a, 0x88, 0xb8);
    for (int i = 0; i + 1 < sl.size(); ++i) {
        if (sl[i].phase != sl[i + 1].phase) continue;
        const QRectF &r0 = geom_[i].card_rect;
        const QRectF &r1 = geom_[i + 1].card_rect;
        const bool same_row = std::abs(r0.top() - r1.top()) < 4.0;
        QPointF s, e;
        bool vertical = false;
        if (same_row) {
            // Source on left, target on right → exit right, enter left.
            const bool left_to_right = r0.center().x() < r1.center().x();
            s = QPointF(left_to_right ? r0.right() : r0.left(), r0.center().y());
            e = QPointF(left_to_right ? r1.left()  : r1.right(), r1.center().y());
        } else {
            // Row-wrap: drop out the bottom of the source, enter the top of
            // the target. dx may be non-zero (next row wraps to col 0), so
            // the curve has a soft S-shape.
            s = QPointF(r0.center().x(), r0.bottom());
            e = QPointF(r1.center().x(), r1.top());
            vertical = true;
        }
        drawBezierArrow(p, s, e, kArrowCol, /*dashed=*/false, vertical);
    }

    // Cross-phase Bezier (last of phase 1 → first of phase 2)
    int p1_last = -1, p2_first = -1;
    for (int i = 0; i < sl.size(); ++i) {
        if (sl[i].phase == 1) p1_last = i;
        if (sl[i].phase == 2 && p2_first < 0) p2_first = i;
    }
    if (p1_last >= 0 && p2_first >= 0) {
        const auto &a = geom_[p1_last].card_rect;
        const auto &b = geom_[p2_first].card_rect;
        // Right side of phase-1 last → left side of phase-2 first
        const QPointF s(a.right(),     a.center().y());
        const QPointF e(b.left(),      b.center().y());
        drawBezierArrow(p, s, e, QColor(0xc0, 0x90, 0x60), /*dashed=*/true);
    }

    // Cards
    for (int i = 0; i < sl.size(); ++i) {
        const auto &stage = sl[i];
        const auto &g     = geom_[i];

        const auto sigMap = status_.value(stage.id);
        bool any_active = false, any_error = false, any_done = false, all_done = true;
        for (const auto &sig : stage.entries) {
            const SignalState st = sigMap.value(sig.id).state;
            if (st == SignalState::Error)  any_error  = true;
            if (st == SignalState::Active) any_active = true;
            if (st == SignalState::Done)   any_done   = true;
            if (st != SignalState::Done)   all_done   = false;
        }
        SignalState worst;
        if (any_error)       worst = SignalState::Error;
        else if (any_active) worst = SignalState::Active;
        else if (all_done)   worst = SignalState::Done;
        else if (any_done)   worst = SignalState::Active;
        else                 worst = SignalState::Pending;
        const QColor edge = stateColor(worst, pulse_phase_);

        // Card body
        p.setBrush(QColor(0x1c, 0x22, 0x32, 230));
        QPen ep(edge); ep.setWidth(worst == SignalState::Active ? 3 : 2);
        p.setPen(ep);
        p.drawRoundedRect(g.card_rect, 8, 8);

        // 单步模式选中: 整张卡片外圈再画一道黄色虚线作为选中标识. 比信号
        // 行级高亮更直观, 也匹配"一步 = 一个卡片"的语义.
        if (!selected_stage_.isEmpty() && stage.id == selected_stage_) {
            QPen yp(QColor(0xff, 0xeb, 0x3b));
            yp.setWidth(3);
            yp.setStyle(Qt::DashLine);
            p.setPen(yp);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(g.card_rect.adjusted(-3, -3, 3, 3), 10, 10);
        }

        // Title bar
        QRectF tr(g.card_rect.left(), g.card_rect.top(), g.card_rect.width(), kTitleH);
        QLinearGradient grad(tr.topLeft(), tr.bottomLeft());
        grad.setColorAt(0, QColor(0x25, 0x2e, 0x44));
        grad.setColorAt(1, QColor(0x1a, 0x21, 0x32));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        QPainterPath titlePath;
        titlePath.addRoundedRect(tr, 8, 8);
        titlePath.addRect(QRectF(tr.left(), tr.bottom() - 8, tr.width(), 8));
        p.drawPath(titlePath);

        f.setPointSize(10); f.setBold(true); p.setFont(f);
        p.setPen(QColor(0xd8, 0xe0, 0xf0));
        const QString head = QStringLiteral("%1  %2").arg(stage.number).arg(stage.title);
        p.drawText(tr.adjusted(10, 0, -28, 0), Qt::AlignVCenter | Qt::AlignLeft, head);

        p.setPen(Qt::NoPen);
        p.setBrush(edge);
        QRectF pill(tr.right() - 22, tr.center().y() - 6, 12, 12);
        p.drawEllipse(pill);

        // Signal rows
        f.setPointSize(8); f.setBold(false); p.setFont(f);
        for (int k = 0; k < stage.entries.size(); ++k) {
            const auto &sig = stage.entries[k];
            const auto &st = sigMap.value(sig.id);
            const QRectF row = g.signal_rects[k];

            // Dot (+ halo when Active)
            QPointF dot(row.left() + 8, row.center().y());
            const QColor c = stateColor(st.state, pulse_phase_);
            if (st.state == SignalState::Active) {
                QColor halo = c; halo.setAlpha(70);
                p.setPen(Qt::NoPen); p.setBrush(halo);
                p.drawEllipse(dot, kDotR + 4, kDotR + 4);
            }
            p.setPen(Qt::NoPen); p.setBrush(c);
            p.drawEllipse(dot, kDotR, kDotR);

            const qreal text_x = row.left() + 22;
            p.setPen(QColor(0xa8, 0xb4, 0xc8));
            p.drawText(QRectF(text_x, row.top(), 72, row.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, sig.label);

            p.setPen(QColor(0x6a, 0x71, 0x80));
            p.drawText(QRectF(text_x + 72, row.top(), 8, row.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, ":");

            QString shown = st.text;
            if (shown.isEmpty()) {
                shown = (st.state == SignalState::Done)    ? QStringLiteral("完成")
                      : (st.state == SignalState::Error)   ? QStringLiteral("故障")
                      : (st.state == SignalState::Active)  ? QStringLiteral("进行中")
                                                            : sig.default_text;
            }
            QColor txt_col = (st.state == SignalState::Done)    ? QColor(0x90, 0xff, 0xc0)
                          : (st.state == SignalState::Error)   ? QColor(0xff, 0xa0, 0xa0)
                          : (st.state == SignalState::Active)  ? QColor(0xa8, 0xc8, 0xff)
                                                                : QColor(0x70, 0x78, 0x88);
            p.setPen(txt_col);
            p.drawText(QRectF(text_x + 80, row.top(),
                              row.width() - 84, row.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, shown);
        }
    }
}

// ────────────────────────────────────────────────────────────────────────
// Mouse / hover
// ────────────────────────────────────────────────────────────────────────

void TaskFlowWidget::mousePressEvent(QMouseEvent *event) {
    // 默认整个流程图是 read-only HMI 显示, 鼠标点击不触发任何动作 —
    // 避免操作员误碰 / 鼠标蹭过去触发执行. 只有 Tab4 切到单步模式时
    // setClickEnabled(true) 才允许点击选卡片.
    if (!click_enabled_) return;
    if (event->button() != Qt::LeftButton) return;

    const QPointF p = event->position();
    // 单步模式选中粒度 = 整张卡片. 只要点击命中 card_rect (包括标题栏 "1
    // 初始化" / "2 视觉定位" 等),就发出该卡片**第一个**子状态的 legacy
    // state_id; Tab4 那边会自动解析成 stage_id 并把整张卡片高亮 + 把该
    // stage 下所有子状态加入待执行队列.
    for (int i = 0; i < geom_.size(); ++i) {
        if (!geom_[i].card_rect.contains(p)) continue;
        const auto &stage = stages()[i];
        for (const auto &m : legacyMap()) {
            if (stage.id == QLatin1String(m.stage)) {
                emit stationClicked(QString::fromLatin1(m.state));
                return;
            }
        }
        return;
    }
}

void TaskFlowWidget::setClickEnabled(bool enabled) {
    if (click_enabled_ == enabled) return;
    click_enabled_ = enabled;
    // 单步模式下把光标变成手型,提示卡片可点;否则恢复箭头
    setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void TaskFlowWidget::mouseMoveEvent(QMouseEvent *event) {
    const QPointF p = event->position();
    int new_card = -1, new_sig = -1;
    QString tip;
    for (int i = 0; i < geom_.size(); ++i) {
        if (!geom_[i].card_rect.contains(p)) continue;
        new_card = i;
        for (int k = 0; k < geom_[i].signal_rects.size(); ++k) {
            if (geom_[i].signal_rects[k].contains(p)) {
                new_sig = k;
                tip = stages()[i].entries[k].label;
                break;
            }
        }
        if (new_sig < 0) tip = stages()[i].title;
        break;
    }
    if (new_card != hover_card_ || new_sig != hover_sig_) {
        hover_card_ = new_card;
        hover_sig_  = new_sig;
        update();
    }
    if (!tip.isEmpty()) QToolTip::showText(event->globalPosition().toPoint(), tip, this);
    else                QToolTip::hideText();
}

void TaskFlowWidget::onPulseTick() {
    pulse_phase_ = (pulse_phase_ + 12) % 360;
    for (const auto &stage : stages()) {
        const auto &m = status_.value(stage.id);
        for (const auto &sig : stage.entries) {
            if (m.value(sig.id).state == SignalState::Active) {
                update();
                return;
            }
        }
    }
}
