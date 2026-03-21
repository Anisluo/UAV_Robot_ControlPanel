#ifndef GRIPPERWIDGET_H
#define GRIPPERWIDGET_H

#include <QGroupBox>

class RpcClient;
class QSlider;
class QSpinBox;
class QPushButton;
class QLabel;

class GripperWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit GripperWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onAirportOpen();
    void onAirportClose();
    void onArmSliderChanged(int value);
    void onArmSpinChanged(int value);
    void onArmGripperSet();

private:
    void buildUi();

    RpcClient   *rpc_;

    // Airport gripper
    QPushButton *btn_ap_open_;
    QPushButton *btn_ap_close_;
    QLabel      *ap_status_label_;

    // Arm gripper
    QSlider     *arm_slider_;
    QSpinBox    *arm_spin_;
    QPushButton *btn_arm_set_;
};

#endif // GRIPPERWIDGET_H
