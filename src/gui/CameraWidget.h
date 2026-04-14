#ifndef CAMERAWIDGET_H
#define CAMERAWIDGET_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QVector>

struct DetectionBox {
    int     class_id = 0;
    float   score    = 0.0F;
    float   x1 = 0.0F, y1 = 0.0F, x2 = 0.0F, y2 = 0.0F;   // pixel coords in frame
    float   x_mm = 0.0F, y_mm = 0.0F, z_mm = 0.0F;        // camera-frame 3D (mm)
    bool    has_xyz  = false;
};

class CameraWidget : public QWidget {
    Q_OBJECT
public:
    explicit CameraWidget(QWidget *parent = nullptr);

public slots:
    void setFrame(const QImage &img);
    void updateFps(double fps);
    void setDetections(const QVector<DetectionBox> &dets);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void onNoSignalTimeout();

    QImage                 current_frame_;
    double                 fps_{0.0};
    bool                   has_signal_{false};
    QTimer                *no_signal_timer_;
    QVector<DetectionBox>  detections_;
};

#endif // CAMERAWIDGET_H
