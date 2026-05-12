#include "TaskFlowWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QLineF>
#include <QRadialGradient>
#include <QtMath>
#include <QToolTip>
#include <cmath>

namespace {

// Single source of truth for the 24-state pipeline. Order matters — the
// metro layout positions stations left→right in this sequence. demo_joints
// is what the sim mode lerps toward when the state becomes "current"; you
// can refine those numbers once real waypoints are recorded.
//
// Phase 1 (line 1) — pull old battery out of the drone and stow it.
// Phase 2 (line 2) — fetch fresh battery from slot and load it into the drone.
const QVector<TaskState>& canonicalStates() {
    static const QVector<TaskState> S = {
        // ── Phase 1: 取下旧电池入库 ─────────────────────────────────
        {"HOME",            "归零",        "上电归零, 全部关节 0°",                1, {0,  0,   0,   0,  0, 0}},
        {"MOVE_MONITOR",    "监视位",      "MoveJ 到对准机场的监视姿态, 摄像头朝向 UAV 降落区", 1, {0, 30, -60,   0, 30, 0}},
        {"CENTERING",       "视觉居中",    "机械臂左右微动, 让 UAV 在摄像头中央",      1, {0, 30, -60,   0, 30, 0}},
        {"DRONE_STATIC",    "等待静止",    "无人机桨叶停转 + 位置静止 N 秒",          1, {0, 30, -60,   0, 30, 0}},
        {"AIRPORT_LOCK",    "机场夹紧",    "机场左右夹爪收拢 → 等堵转 → UAV 锁住",   1, {0, 32, -62,   0, 35, 0}},
        {"APPROACH_DRONE",  "接近电池",    "MoveL 沿固定方向插入到电池抓取深度",       1, {-15, 40, -75, 0, 50, 0}},
        {"GRAB_LIGHT",      "轻夹电池",    "piper 夹爪轻力夹住电池 (小力, 不变形)",   1, {-15, 40, -75, 0, 50, 0}},
        {"EXTRACT",         "抽出电池",    "MoveL 反向直线退出, 把电池抽出来",         1, {-10, 35, -70, 0, 45, 0}},
        {"REGRIP_FIRM",     "重夹大力",    "调整角度后用更大力度重新夹紧",             1, { -5, 30, -65, 0, 40, 0}},
        {"AIRPORT_RELEASE", "机场松开",    "机场夹爪打开, 释放 UAV",                  1, { -5, 30, -65, 0, 40, 0}},
        {"MOVE_TO_PLATFORM","搬到平台",    "MoveJ 把电池搬到平台安全位",               1, { 30, 35, -65, 0, 35, 0}},
        {"ADJUST_HEAD",     "调头夹住",    "调整角度, 夹住电池头部",                   1, { 30, 35, -65, 0, 35, 0}},
        {"INSERT_SLOT",     "插入电池槽",  "MoveL 直线插入电池槽",                     1, { 45, 40, -70, 0, 30, 0}},
        {"RELEASE_IN_SLOT", "槽内松开",    "电池入位 → piper 夹爪打开",               1, { 45, 40, -70, 0, 30, 0}},

        // ── Phase 2: 取新电池装回 ──────────────────────────────────
        {"FETCH_NEW_BAT",   "取新电池",    "MoveJ 回电池槽, 夹住新电池头部",           2, { 45, 40, -70, 0, 30, 0}},
        {"PLACE_PLATFORM",  "放回平台",    "MoveL 抽出 → 放在平台安全位",              2, { 30, 35, -65, 0, 35, 0}},
        {"REGRIP_TAIL",     "夹住尾部",    "调整角度, 重新夹住电池尾部",               2, { 30, 35, -65, 0, 35, 0}},
        {"MOVE_HANDOFF",    "送交接位",    "MoveJ 送到机场夹爪交接位置",               2, {  5, 32, -62, 0, 38, 0}},
        {"AIRPORT_LOCK_NEW","机场接住",    "机场夹爪收拢 → 等堵转 → 接住新电池",      2, {  5, 32, -62, 0, 38, 0}},
        {"ARM_RELEASE",     "机械臂松开",  "piper 夹爪打开 → 退离",                  2, {  5, 30, -60, 0, 35, 0}},
        {"RETURN_MONITOR_2","回监视位",    "MoveJ 回到监视姿态",                      2, {  0, 30, -60, 0, 30, 0}},
        {"AIRPORT_INSERT",  "推入无人机",  "机场夹爪把新电池推入 UAV",                 2, {  0, 30, -60, 0, 30, 0}},
        {"WAIT_TAKEOFF",    "等待起飞",    "电池就位, 等 UAV 起飞确认",               2, {  0, 30, -60, 0, 30, 0}},
        {"DONE",            "完成",        "任务完成, 回 idle",                       2, {  0,  0,   0, 0,  0, 0}},
    };
    return S;
}

}  // namespace


const QVector<TaskState>& TaskFlowWidget::states() {
    return canonicalStates();
}


// ════════════════════════════════════════════════════════════════════════
// Construction
// ════════════════════════════════════════════════════════════════════════
TaskFlowWidget::TaskFlowWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumSize(720, 280);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // Initialise all states to pending.
    for (const auto &s : states()) status_[s.id] = StateStatus::Pending;

    pulse_timer_ = new QTimer(this);
    pulse_timer_->setInterval(40);     // ~25 fps glow
    connect(pulse_timer_, &QTimer::timeout, this, &TaskFlowWidget::onPulseTick);
    pulse_timer_->start();
}


// ════════════════════════════════════════════════════════════════════════
// Public API — owner mutates visual state
// ════════════════════════════════════════════════════════════════════════
void TaskFlowWidget::setCurrentState(const QString &state_id) {
    // Demote the previous "current" (if any) back to whatever its post-state was.
    // We treat moving forward as implicitly marking the previous state Done,
    // since the state machine on the backend only ever advances.
    if (!current_.isEmpty() && current_ != state_id) {
        if (status_.value(current_) == StateStatus::Current) {
            status_[current_] = StateStatus::Done;
        }
    }
    current_ = state_id;
    if (!state_id.isEmpty() && status_.value(state_id) != StateStatus::Error) {
        status_[state_id] = StateStatus::Current;
    }
    update();
}

void TaskFlowWidget::markFinished(const QString &state_id) {
    status_[state_id] = StateStatus::Done;
    if (current_ == state_id) current_.clear();
    update();
}

void TaskFlowWidget::markError(const QString &state_id, const QString &error_msg) {
    status_[state_id] = StateStatus::Error;
    errors_[state_id] = error_msg;
    update();
}

void TaskFlowWidget::resetAll() {
    for (const auto &s : states()) status_[s.id] = StateStatus::Pending;
    current_.clear();
    errors_.clear();
    update();
}


// ════════════════════════════════════════════════════════════════════════
// Layout — recompute station geometry every paint (cheap enough; widget
// resizes are rare and the math is trivial).
// ════════════════════════════════════════════════════════════════════════
void TaskFlowWidget::recomputeGeometry() {
    const auto &S = states();
    geom_.clear();
    geom_.reserve(S.size());

    // Count stations per phase to space them evenly.
    int n_phase1 = 0, n_phase2 = 0;
    for (const auto &s : S) (s.phase == 1 ? n_phase1 : n_phase2)++;

    const double W = width();
    const double H = height();

    // Two horizontal "metro lines": phase 1 in the top third, phase 2 in
    // the bottom third. Leave generous margins for the labels under each
    // station.
    const double margin_x  = 56.0;
    const double row1_y    = H * 0.30;
    const double row2_y    = H * 0.72;
    const double radius    = qMax(12.0, qMin(W / 80.0, 22.0));

    const double usable_w  = W - 2 * margin_x;
    const double step1     = (n_phase1 > 1) ? usable_w / (n_phase1 - 1) : 0;
    const double step2     = (n_phase2 > 1) ? usable_w / (n_phase2 - 1) : 0;

    int seen1 = 0, seen2 = 0;
    for (int i = 0; i < S.size(); ++i) {
        const auto &s = S[i];
        StationGeom g{};
        g.radius = radius;
        g.idx    = i;
        if (s.phase == 1) {
            g.center = QPointF(margin_x + seen1 * step1, row1_y);
            seen1++;
        } else {
            g.center = QPointF(margin_x + seen2 * step2, row2_y);
            seen2++;
        }
        geom_.push_back(g);
    }
}


// ════════════════════════════════════════════════════════════════════════
// Paint
// ════════════════════════════════════════════════════════════════════════
void TaskFlowWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(15, 16, 24));      // matches the dashboard's #0f1018

    recomputeGeometry();

    const auto &S = states();

    // Row title labels
    p.setFont(QFont("微软雅黑", 11, QFont::Bold));
    p.setPen(QColor(180, 190, 220));
    p.drawText(QRectF(20, height() * 0.30 - 50, 200, 20), Qt::AlignLeft,
               "Phase 1 · 取下旧电池入库");
    p.drawText(QRectF(20, height() * 0.72 - 50, 200, 20), Qt::AlignLeft,
               "Phase 2 · 取新电池装回");

    // 1) Draw connectors first (so circles sit on top).
    int prev_phase = -1;
    QPointF prev_center;
    for (int i = 0; i < S.size(); ++i) {
        const auto &g = geom_[i];
        if (S[i].phase != prev_phase) {
            prev_phase = S[i].phase;
            prev_center = g.center;
            continue;
        }
        // line color: gradient grey → bright if both endpoints are done
        const auto status_a = status_.value(S[i - 1].id);
        const auto status_b = status_.value(S[i].id);
        QColor line_color;
        if (status_a == StateStatus::Done && status_b == StateStatus::Done)
            line_color = QColor(76, 175, 80);
        else if (status_a == StateStatus::Done || status_a == StateStatus::Current)
            line_color = QColor(110, 130, 170);
        else
            line_color = QColor(70, 78, 100);
        p.setPen(QPen(line_color, 4));
        p.drawLine(prev_center, g.center);
        prev_center = g.center;
    }

    // Crossover line from end of phase 1 to start of phase 2 (drawn as
    // a curved/elbow to suggest "phase boundary, not direct movement").
    int phase1_last_idx = -1, phase2_first_idx = -1;
    for (int i = 0; i < S.size(); ++i) {
        if (S[i].phase == 1) phase1_last_idx = i;
        if (S[i].phase == 2 && phase2_first_idx < 0) phase2_first_idx = i;
    }
    if (phase1_last_idx >= 0 && phase2_first_idx >= 0) {
        const QPointF a = geom_[phase1_last_idx].center;
        const QPointF b = geom_[phase2_first_idx].center;
        QColor connector(110, 110, 130);
        p.setPen(QPen(connector, 3, Qt::DashLine));
        // S-curve via two intermediate points
        const QPointF c1(a.x(),       (a.y() + b.y()) / 2.0);
        const QPointF c2(b.x(),       (a.y() + b.y()) / 2.0);
        QPainterPath path;
        path.moveTo(a);
        path.cubicTo(c1, c2, b);
        p.drawPath(path);
    }

    // 2) Draw stations
    p.setFont(QFont("微软雅黑", 9));
    for (int i = 0; i < S.size(); ++i) {
        const auto &s = S[i];
        const auto &g = geom_[i];
        const StateStatus st = status_.value(s.id);

        // Pulse glow under current state
        if (st == StateStatus::Current) {
            const double pulse_factor = 0.5 + 0.5 * std::sin(pulse_phase_ * M_PI / 180.0);
            QRadialGradient grad(g.center, g.radius * 2.2);
            QColor glow(33, 150, 243);
            glow.setAlphaF(0.55 * pulse_factor);
            grad.setColorAt(0, glow);
            glow.setAlphaF(0);
            grad.setColorAt(1, glow);
            p.setBrush(grad);
            p.setPen(Qt::NoPen);
            p.drawEllipse(g.center, g.radius * 2.2, g.radius * 2.2);
        }

        // Station fill color
        QColor fill, border;
        switch (st) {
            case StateStatus::Pending:
                fill   = QColor(55, 60, 80);
                border = QColor(110, 120, 145);
                break;
            case StateStatus::Current:
                fill   = QColor(33, 150, 243);
                border = QColor(180, 220, 255);
                break;
            case StateStatus::Done:
                fill   = QColor(76, 175, 80);
                border = QColor(180, 230, 180);
                break;
            case StateStatus::Error:
                fill   = QColor(244, 67, 54);
                border = QColor(255, 200, 200);
                break;
        }
        // Hover ring
        if (hover_idx_ == i) border = QColor(255, 200, 80);

        p.setBrush(fill);
        p.setPen(QPen(border, 2));
        p.drawEllipse(g.center, g.radius, g.radius);

        // Station index inside circle
        p.setPen(QColor(245, 248, 252));
        p.setFont(QFont("微软雅黑", 9, QFont::Bold));
        p.drawText(QRectF(g.center.x() - g.radius,
                          g.center.y() - g.radius,
                          g.radius * 2, g.radius * 2),
                    Qt::AlignCenter,
                    QString::number(i + 1));

        // Station label below the circle (alternate offset to prevent overlap)
        p.setFont(QFont("微软雅黑", 9));
        p.setPen(QColor(220, 230, 245));
        const double label_y = g.center.y() + g.radius + (i % 2 == 0 ? 6 : 22);
        p.drawText(QRectF(g.center.x() - 50, label_y, 100, 18),
                    Qt::AlignCenter, s.label);

        // Tick mark for done states
        if (st == StateStatus::Done) {
            p.setPen(QPen(QColor(245, 248, 252), 2));
            QPointF p1(g.center.x() - g.radius * 0.45, g.center.y() + g.radius * 0.05);
            QPointF p2(g.center.x() - g.radius * 0.10, g.center.y() + g.radius * 0.40);
            QPointF p3(g.center.x() + g.radius * 0.55, g.center.y() - g.radius * 0.30);
            p.drawLine(p1, p2);
            p.drawLine(p2, p3);
        }
        // ✗ mark for error states
        if (st == StateStatus::Error) {
            p.setPen(QPen(QColor(245, 248, 252), 2));
            const double r = g.radius * 0.55;
            p.drawLine(g.center.x() - r, g.center.y() - r,
                       g.center.x() + r, g.center.y() + r);
            p.drawLine(g.center.x() + r, g.center.y() - r,
                       g.center.x() - r, g.center.y() + r);
        }
    }

    // Legend
    p.setFont(QFont("微软雅黑", 8));
    p.setPen(QColor(180, 190, 220));
    const int legend_y = height() - 18;
    int x = 12;
    auto drawLegendDot = [&](QColor c, const char *label) {
        p.setBrush(c);
        p.setPen(QPen(c.lighter(140), 1));
        p.drawEllipse(QPointF(x, legend_y), 5, 5);
        p.setPen(QColor(200, 210, 230));
        p.drawText(x + 9, legend_y + 4, label);
        x += int(p.fontMetrics().horizontalAdvance(label)) + 26;
    };
    drawLegendDot(QColor(55, 60, 80),  "未到");
    drawLegendDot(QColor(33, 150, 243), "执行中");
    drawLegendDot(QColor(76, 175, 80), "已完成");
    drawLegendDot(QColor(244, 67, 54), "失败");
}


// ════════════════════════════════════════════════════════════════════════
// Pulse animation
// ════════════════════════════════════════════════════════════════════════
void TaskFlowWidget::onPulseTick() {
    pulse_phase_ = (pulse_phase_ + 6) % 360;
    // Only repaint if there is a current state (otherwise nothing animates).
    if (!current_.isEmpty()) update();
}


// ════════════════════════════════════════════════════════════════════════
// Mouse interaction
// ════════════════════════════════════════════════════════════════════════
void TaskFlowWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    for (const auto &g : geom_) {
        if (QLineF(g.center, event->position()).length() <= g.radius) {
            emit stationClicked(states()[g.idx].id);
            return;
        }
    }
}

void TaskFlowWidget::mouseMoveEvent(QMouseEvent *event) {
    int new_hover = -1;
    QString tooltip;
    for (const auto &g : geom_) {
        if (QLineF(g.center, event->position()).length() <= g.radius * 1.3) {
            new_hover = g.idx;
            const auto &s = states()[g.idx];
            tooltip = QString("<b>%1 · %2</b><br>%3").arg(g.idx + 1).arg(s.label, s.desc);
            const auto err_it = errors_.constFind(s.id);
            if (err_it != errors_.constEnd()) {
                tooltip += QString("<br><span style='color:#f44'>错误: %1</span>").arg(*err_it);
            }
            break;
        }
    }
    if (new_hover != hover_idx_) {
        hover_idx_ = new_hover;
        update();
    }
    if (!tooltip.isEmpty()) QToolTip::showText(event->globalPosition().toPoint(), tooltip, this);
    else                    QToolTip::hideText();
}
