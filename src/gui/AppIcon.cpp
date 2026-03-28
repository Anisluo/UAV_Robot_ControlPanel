#include "AppIcon.h"

#include <QBrush>
#include <QColor>
#include <QGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

namespace {
QPainterPath droneBodyPath()
{
    QPainterPath path;
    path.moveTo(128, 98);
    path.lineTo(146, 116);
    path.lineTo(146, 138);
    path.lineTo(128, 156);
    path.lineTo(110, 138);
    path.lineTo(110, 116);
    path.closeSubpath();
    return path;
}

QPainterPath screenPath()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(74, 166, 108, 34), 10, 10);
    return path;
}
}

QIcon createAppIcon()
{
    QPixmap pixmap(256, 256);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QLinearGradient bg(24, 24, 232, 232);
    bg.setColorAt(0.0, QColor("#0a2230"));
    bg.setColorAt(0.5, QColor("#10384a"));
    bg.setColorAt(1.0, QColor("#07151f"));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(16, 16, 224, 224), 48, 48);

    QRadialGradient glow(128, 108, 104);
    glow.setColorAt(0.0, QColor(0, 210, 255, 52));
    glow.setColorAt(0.55, QColor(0, 210, 255, 12));
    glow.setColorAt(1.0, QColor(0, 210, 255, 0));
    p.setBrush(glow);
    p.drawEllipse(QPointF(128, 108), 92, 92);

    QPen ringPen(QColor("#35d9ff"));
    ringPen.setWidthF(5.0);
    p.setPen(ringPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(128, 108), 64, 64);

    ringPen.setColor(QColor(53, 217, 255, 110));
    ringPen.setWidthF(2.5);
    p.setPen(ringPen);
    p.drawEllipse(QPointF(128, 108), 88, 88);
    p.drawEllipse(QPointF(128, 108), 40, 40);

    QPen gridPen(QColor(53, 217, 255, 90));
    gridPen.setWidthF(2.0);
    p.setPen(gridPen);
    p.drawLine(QPointF(40, 108), QPointF(216, 108));
    p.drawLine(QPointF(128, 42), QPointF(128, 174));
    p.drawLine(QPointF(66, 64), QPointF(190, 152));
    p.drawLine(QPointF(66, 152), QPointF(190, 64));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#f4fbff"));
    p.drawEllipse(QPointF(74, 70), 11, 11);
    p.drawEllipse(QPointF(182, 70), 11, 11);
    p.drawEllipse(QPointF(74, 146), 11, 11);
    p.drawEllipse(QPointF(182, 146), 11, 11);

    QPen armPen(QColor("#f4fbff"));
    armPen.setWidthF(10.0);
    armPen.setCapStyle(Qt::RoundCap);
    p.setPen(armPen);
    p.drawLine(QPointF(128, 126), QPointF(74, 70));
    p.drawLine(QPointF(128, 126), QPointF(182, 70));
    p.drawLine(QPointF(128, 126), QPointF(74, 146));
    p.drawLine(QPointF(128, 126), QPointF(182, 146));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#00d6ff"));
    p.drawPath(droneBodyPath());

    p.setBrush(QColor("#0b1b25"));
    p.drawRoundedRect(QRectF(119, 120, 18, 14), 4, 4);

    p.setBrush(QColor("#f7b733"));
    p.drawEllipse(QPointF(128, 108), 6, 6);

    p.setBrush(QColor(7, 21, 31, 210));
    p.drawPath(screenPath());

    QPen screenPen(QColor("#6ce7ff"));
    screenPen.setWidthF(3.0);
    p.setPen(screenPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(screenPath());

    p.setPen(QPen(QColor("#6ce7ff"), 2.2));
    p.drawLine(QPointF(90, 183), QPointF(112, 183));
    p.drawLine(QPointF(118, 183), QPointF(138, 176));
    p.drawLine(QPointF(138, 176), QPointF(165, 190));

    p.setPen(QPen(QColor("#f7b733"), 3.0));
    p.drawEllipse(QPointF(178, 183), 4.5, 4.5);

    QPen basePen(QColor("#f4fbff"));
    basePen.setWidthF(5.0);
    basePen.setCapStyle(Qt::RoundCap);
    p.setPen(basePen);
    p.drawLine(QPointF(128, 200), QPointF(128, 214));
    p.drawLine(QPointF(106, 214), QPointF(150, 214));

    return QIcon(pixmap);
}
