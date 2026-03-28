#ifndef GRIPPERWIDGET_H
#define GRIPPERWIDGET_H

#include <QGroupBox>

class RpcClient;
class QPushButton;
class QLabel;

class GripperWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit GripperWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onAirportOpen();
    void onAirportClose();
    void onArmOpen();
    void onArmClose();

private:
    void buildUi();

    RpcClient   *rpc_;

    // Airport gripper
    QPushButton *btn_ap_open_;
    QPushButton *btn_ap_close_;
    QLabel      *ap_status_label_;

    // Arm gripper
    QPushButton *btn_arm_open_;
    QPushButton *btn_arm_close_;
    QLabel      *arm_status_label_;
};

#endif // GRIPPERWIDGET_H
