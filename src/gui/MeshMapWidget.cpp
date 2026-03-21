#include "MeshMapWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QFontMetrics>
#include <cmath>

MeshMapWidget::MeshMapWidget(QWidget *parent)
    : QGroupBox("无线通信网络", parent)
{
    setMinimumHeight(200);
    setMaximumHeight(200);
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

    // 5 demo nodes spread across the widget
    MeshNode n0; n0.id = 0; n0.x = 0.15f; n0.y = 0.5f;  n0.rssi = -40; n0.reachable = true;
    MeshNode n1; n1.id = 1; n1.x = 0.38f; n1.y = 0.2f;  n1.rssi = -55; n1.reachable = true;
    MeshNode n2; n2.id = 2; n2.x = 0.55f; n2.y = 0.75f; n2.rssi = -65; n2.reachable = true;
    MeshNode n3; n3.id = 3; n3.x = 0.75f; n3.y = 0.3f;  n3.rssi = -75; n3.reachable = false;
    MeshNode n4; n4.id = 4; n4.x = 0.88f; n4.y = 0.65f; n4.rssi = -50; n4.reachable = true;

    nodes_ << n0 << n1 << n2 << n3 << n4;
}

QPointF MeshMapWidget::nodePos(const MeshNode &n) const
{
    // Map canvas = inside the group box content area
    int margin = 26;
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

    // Draw edges between all reachable nodes
    for (int i = 0; i < nodes_.size(); ++i) {
        for (int j = i + 1; j < nodes_.size(); ++j) {
            const MeshNode &a = nodes_[i];
            const MeshNode &b = nodes_[j];
            if (!a.reachable || !b.reachable) continue;

            int avgRssi = (a.rssi + b.rssi) / 2;
            int thickness = rssiToThickness(avgRssi);
            QColor lineColor = rssiToColor(avgRssi);
            lineColor.setAlpha(160);

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

    const int NODE_RADIUS = 8;

    for (const MeshNode &n : nodes_) {
        QPointF pos = nodePos(n);

        // Node circle
        QColor nodeColor = n.reachable ? QColor(0x4c, 0xaf, 0x50) : QColor(0x55, 0x57, 0x70);
        QColor borderColor = n.reachable ? QColor(0x00, 0xc8, 0xd7) : QColor(0x35, 0x36, 0x50);

        p.setPen(QPen(borderColor, 1.5));
        p.setBrush(nodeColor);
        p.drawEllipse(pos, NODE_RADIUS, NODE_RADIUS);

        // Node ID label
        QString label = QString("N%1").arg(n.id);
        QRect textBound = fm.boundingRect(label);
        int tx = static_cast<int>(pos.x()) - textBound.width() / 2;
        int ty = static_cast<int>(pos.y()) + NODE_RADIUS + textBound.height() + 1;

        p.setPen(QColor(0xdd, 0xe1, 0xf0));
        p.drawText(tx, ty, label);

        // RSSI label
        if (n.reachable) {
            QString rssiStr = QString("%1").arg(n.rssi);
            QRect rBound = fm.boundingRect(rssiStr);
            int rx = static_cast<int>(pos.x()) - rBound.width() / 2;
            int ry = static_cast<int>(pos.y()) - NODE_RADIUS - 3;
            p.setPen(rssiToColor(n.rssi));
            p.drawText(rx, ry, rssiStr);
        }
    }
}
