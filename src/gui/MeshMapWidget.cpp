#include "MeshMapWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QFontMetrics>
#include <cmath>

MeshMapWidget::MeshMapWidget(QWidget *parent)
    : QGroupBox("无线通信网络", parent)
{
    setMinimumHeight(240);
    setMaximumHeight(240);
    populateDemoNodes();
}

void MeshMapWidget::updateNodes(const QList<MeshNode> &nodes)
{
    nodes_ = nodes;
    update();
}

void MeshMapWidget::populateDemoNodes()
{
    nodes_.clear();

    // Placeholder mesh layout:
    // .101 = host / gateway
    // .102~.106 = UAV nodes
    static const struct { int id; float x; float y; } kLayout[] = {
        { 101, 0.50f, 0.18f },
        { 102, 0.14f, 0.58f },
        { 103, 0.32f, 0.78f },
        { 104, 0.50f, 0.60f },
        { 105, 0.68f, 0.78f },
        { 106, 0.86f, 0.58f },
    };
    for (const auto &k : kLayout) {
        MeshNode n;
        n.id        = k.id;
        n.x         = k.x;
        n.y         = k.y;
        n.rssi      = 0;
        n.reachable = false;
        nodes_ << n;
    }
}

QPointF MeshMapWidget::nodePos(const MeshNode &n) const
{
    // Map canvas = inside the group box content area
    int margin = 30;
    int cx = static_cast<int>(margin + n.x * (width()  - margin * 2));
    int cy = static_cast<int>(margin + n.y * (height() - margin * 2));
    return QPointF(cx, cy);
}

int MeshMapWidget::rssiToThickness(int rssi) const
{
    // rssi: -30 (strong) to -90 (weak)
    // clamp and map to 1..4
    int clamped = qMax(-90, qMin(-30, rssi));
    double t = (double)(clamped + 30) / (-90.0 + 30.0); // 0=strong, 1=weak
    return static_cast<int>(1 + (1.0 - t) * 3.0);
}

QColor MeshMapWidget::rssiToColor(int rssi) const
{
    int clamped = qMax(-90, qMin(-30, rssi));
    double t = (double)(clamped + 30) / (-90.0 + 30.0); // 0=strong(cyan), 1=weak(gray)
    int r = static_cast<int>(0x00 + t * (0x55 - 0x00));
    int g = static_cast<int>(0xc8 + t * (0x57 - 0xc8));
    int b = static_cast<int>(0xd7 + t * (0x70 - 0xd7));
    return QColor(r, g, b);
}

void MeshMapWidget::paintEvent(QPaintEvent *event)
{
    // Draw the group box frame first
    QGroupBox::paintEvent(event);

    if (nodes_.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Draw the static mesh skeleton first so all 6 nodes always appear connected.
    auto drawSkeletonLink = [&p, this](const MeshNode &a, const MeshNode &b) {
        QPen pen(QColor(0x66, 0x6b, 0x7f, 180), 1.0);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawLine(nodePos(a), nodePos(b));
    };

    const MeshNode *hostNode = nullptr;
    for (const MeshNode &n : nodes_) {
        if (n.id == 101) {
            hostNode = &n;
            break;
        }
    }

    if (hostNode) {
        for (const MeshNode &n : nodes_) {
            if (n.id == 101) {
                continue;
            }
            drawSkeletonLink(*hostNode, n);
        }
    }

    for (int i = 0; i < nodes_.size(); ++i) {
        for (int j = i + 1; j < nodes_.size(); ++j) {
            const MeshNode &a = nodes_[i];
            const MeshNode &b = nodes_[j];
            if (a.id == 101 || b.id == 101) continue;
            drawSkeletonLink(a, b);
        }
    }

    // Draw host-to-node links for active mesh nodes.
    if (hostNode && hostNode->reachable) {
        for (const MeshNode &n : nodes_) {
            if (n.id == 101 || !n.reachable) {
                continue;
            }

            int avgRssi = (hostNode->rssi + n.rssi) / 2;
            if (avgRssi == 0) {
                avgRssi = -50;
            }
            int thickness = rssiToThickness(avgRssi);
            QColor lineColor = rssiToColor(avgRssi);
            lineColor.setAlpha(170);

            QPen pen(lineColor, thickness);
            p.setPen(pen);
            p.drawLine(nodePos(*hostNode), nodePos(n));
        }
    }

    // Draw lateral links between active UAV nodes for a mesh feel.
    for (int i = 0; i < nodes_.size(); ++i) {
        for (int j = i + 1; j < nodes_.size(); ++j) {
            const MeshNode &a = nodes_[i];
            const MeshNode &b = nodes_[j];
            if (a.id == 101 || b.id == 101) continue;
            if (!a.reachable || !b.reachable) continue;

            int avgRssi = (a.rssi + b.rssi) / 2;
            if (avgRssi == 0) {
                avgRssi = -55;
            }
            int thickness = rssiToThickness(avgRssi);
            QColor lineColor = rssiToColor(avgRssi);
            lineColor.setAlpha(80);

            QPen pen(lineColor, thickness);
            p.setPen(pen);
            p.drawLine(nodePos(a), nodePos(b));
        }
    }

    // Draw nodes
    QFont labelFont;
    labelFont.setFamily("Consolas");
    labelFont.setPointSize(8);
    p.setFont(labelFont);
    QFontMetrics fm(labelFont);

    const int NODE_RADIUS = 10;

    for (const MeshNode &n : nodes_) {
        QPointF pos = nodePos(n);

        // Node circle
        QColor nodeColor = n.reachable ? QColor(0x4c, 0xaf, 0x50) : QColor(0x55, 0x57, 0x70);
        QColor borderColor = n.reachable ? QColor(0x98, 0xe2, 0x8d) : QColor(0x35, 0x36, 0x50);

        p.setPen(QPen(borderColor, 1.5));
        p.setBrush(nodeColor);
        p.drawEllipse(pos, NODE_RADIUS, NODE_RADIUS);

        // Node ID label — for IDs >= 100 show as ".101", else "N1"
        QString label = (n.id >= 100)
                        ? QString(".%1").arg(n.id)
                        : QString("N%1").arg(n.id);
        QRect textBound = fm.boundingRect(label);
        int tx = static_cast<int>(pos.x()) - textBound.width() / 2;
        int ty = static_cast<int>(pos.y()) + NODE_RADIUS + textBound.height() + 1;

        p.setPen(QColor(0xdd, 0xe1, 0xf0));
        p.drawText(tx, ty, label);

        QString status = n.reachable ? "activate" : "inactive";
        QString role = (n.id == 101) ? "HOST" : "UAV";
        QString subLabel = QString("%1  %2").arg(role, status);
        QRect subBound = fm.boundingRect(subLabel);
        int sx = static_cast<int>(pos.x()) - subBound.width() / 2;
        int sy = ty + textBound.height() + 1;
        p.setPen(n.reachable ? QColor(0x98, 0xe2, 0x8d) : QColor(0x7f, 0x8a, 0xa3));
        p.drawText(sx, sy, subLabel);

        // RSSI label (omit when rssi == 0, e.g. ping-only nodes)
        if (n.reachable && n.rssi != 0) {
            QString rssiStr = QString("%1").arg(n.rssi);
            QRect rBound = fm.boundingRect(rssiStr);
            int rx = static_cast<int>(pos.x()) - rBound.width() / 2;
            int ry = static_cast<int>(pos.y()) - NODE_RADIUS - 3;
            p.setPen(rssiToColor(n.rssi));
            p.drawText(rx, ry, rssiStr);
        }
    }
}
