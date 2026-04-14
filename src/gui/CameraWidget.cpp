#include "CameraWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QFontMetrics>

CameraWidget::CameraWidget(QWidget *parent)
    : QWidget(parent)
    , no_signal_timer_(new QTimer(this))
{
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);

    no_signal_timer_->setSingleShot(true);
    no_signal_timer_->setInterval(3000); // 3 seconds without frame = no signal
    connect(no_signal_timer_, &QTimer::timeout, this, &CameraWidget::onNoSignalTimeout);
}

void CameraWidget::setFrame(const QImage &img)
{
    current_frame_ = img;
    has_signal_    = true;
    no_signal_timer_->start();
    update();
}

void CameraWidget::updateFps(double fps)
{
    fps_ = fps;
    update();
}

void CameraWidget::setDetections(const QVector<DetectionBox> &dets)
{
    detections_ = dets;
    update();
}

void CameraWidget::onNoSignalTimeout()
{
    has_signal_ = false;
    update();
}

void CameraWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Background
    p.fillRect(rect(), QColor(0x00, 0x00, 0x00));

    if (!has_signal_ || current_frame_.isNull()) {
        // NO SIGNAL overlay
        QFont f = p.font();
        f.setPointSize(18);
        f.setBold(true);
        f.setFamily("Consolas");
        p.setFont(f);
        p.setPen(QColor(0x35, 0x36, 0x50));
        p.drawText(rect(), Qt::AlignCenter, "无信号");
        return;
    }

    // Draw frame maintaining aspect ratio
    QRect target;
    {
        int w = width();
        int h = height();
        double imgAspect = (double)current_frame_.width() / current_frame_.height();
        double widAspect = (double)w / h;
        int tw, th;
        if (imgAspect > widAspect) {
            tw = w;
            th = (int)(w / imgAspect);
        } else {
            th = h;
            tw = (int)(h * imgAspect);
        }
        int tx = (w - tw) / 2;
        int ty = (h - th) / 2;
        target = QRect(tx, ty, tw, th);
    }
    p.drawImage(target, current_frame_);

    // ── Draw detection boxes over the frame ─────────────────────────────
    if (!detections_.isEmpty() && !current_frame_.isNull()) {
        const double sx = (double)target.width()  / current_frame_.width();
        const double sy = (double)target.height() / current_frame_.height();
        QFont bf = p.font();
        bf.setFamily("Consolas");
        bf.setPointSize(9);
        bf.setBold(true);
        p.setFont(bf);
        QFontMetrics bfm(bf);

        for (const DetectionBox &d : detections_) {
            int rx = target.x() + (int)(d.x1 * sx);
            int ry = target.y() + (int)(d.y1 * sy);
            int rw = (int)((d.x2 - d.x1) * sx);
            int rh = (int)((d.y2 - d.y1) * sy);
            QRect box(rx, ry, rw, rh);

            // Colored rectangle — bright green for normal, orange for has_xyz.
            QColor c = d.has_xyz ? QColor(0xff, 0x9f, 0x00) : QColor(0x4c, 0xaf, 0x50);
            QPen pen(c);
            pen.setWidth(2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(box);

            // Label background + text
            QString label;
            if (d.has_xyz) {
                label = QString("#%1 %2%  Z=%3mm")
                            .arg(d.class_id)
                            .arg((int)(d.score * 100))
                            .arg((int)d.z_mm);
            } else {
                label = QString("#%1 %2%").arg(d.class_id).arg((int)(d.score * 100));
            }
            QRect tr = bfm.boundingRect(label);
            int pad = 3;
            QRect bgR(rx, ry - tr.height() - 2*pad,
                      tr.width() + 2*pad, tr.height() + 2*pad);
            if (bgR.y() < target.y()) {
                bgR.moveTop(ry + 2);  // flip below if too close to top
            }
            p.fillRect(bgR, QColor(0, 0, 0, 180));
            p.setPen(c);
            p.drawText(bgR, Qt::AlignCenter, label);
        }
    }

    // Info overlay (top-right)
    QString info = QString("%1x%2  %3 帧率")
                   .arg(current_frame_.width())
                   .arg(current_frame_.height())
                   .arg(fps_, 0, 'f', 1);

    QFont f = p.font();
    f.setFamily("Consolas");
    f.setPointSize(9);
    p.setFont(f);
    QFontMetrics fm(f);
    QRect textRect = fm.boundingRect(info);
    int padding = 6;
    int tx2 = width() - textRect.width() - padding * 2;
    int ty2 = 4;
    QRect bgRect(tx2, ty2, textRect.width() + padding * 2, textRect.height() + padding);
    p.fillRect(bgRect, QColor(0, 0, 0, 160));
    p.setPen(QColor(0x00, 0xc8, 0xd7));
    p.drawText(bgRect, Qt::AlignCenter, info);
}

void CameraWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}
