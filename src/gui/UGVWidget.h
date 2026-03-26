#ifndef UGVWIDGET_H
#define UGVWIDGET_H

#include <QGroupBox>

class RpcClient;
class QSlider;
class QDoubleSpinBox;
class QPushButton;

class UGVWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit UGVWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onVxSliderChanged(int value);
    void onVxSpinChanged(double value);
    void onOmegaSliderChanged(int value);
    void onOmegaSpinChanged(double value);
    void sendVelocity();
    void onStop();

private:
    void buildUi();
    void sendVelocity(double vx, double omega);
    void resetInputs();

    RpcClient      *rpc_;

    QSlider        *vx_slider_;
    QDoubleSpinBox *vx_spin_;
    QSlider        *omega_slider_;
    QDoubleSpinBox *omega_spin_;

    QPushButton    *btn_send_;
    QPushButton    *btn_stop_;
    QPushButton    *btn_forward_;
    QPushButton    *btn_backward_;
    QPushButton    *btn_turn_left_;
    QPushButton    *btn_turn_right_;

    // Slider uses integer scaled by 100 for float precision
    static constexpr int   VELOCITY_SCALE = 100;
    static constexpr double OMEGA_SCALE   = 100.0;
};

#endif // UGVWIDGET_H
