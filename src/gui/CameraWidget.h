#ifndef CAMERAWIDGET_H
#define CAMERAWIDGET_H

#include <QWidget>
#include <QImage>
#include <QTimer>

class CameraWidget : public QWidget {
    Q_OBJECT
public:
    explicit CameraWidget(QWidget *parent = nullptr);

public slots:
    void setFrame(const QImage &img);
    void updateFps(double fps);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void onNoSignalTimeout();

    QImage   current_frame_;
    double   fps_{0.0};
    bool     has_signal_{false};
    QTimer  *no_signal_timer_;
};

#endif // CAMERAWIDGET_H
