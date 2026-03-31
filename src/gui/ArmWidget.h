#ifndef ARMWIDGET_H
#define ARMWIDGET_H

#include <QGroupBox>
#include <QList>

class RpcClient;
class QDoubleSpinBox;
class QPushButton;
class QTimer;
class QLabel;

class ArmWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit ArmWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onSet();
    void onHome();
    void onHomeAxis();
    void onMoveAxis();
    void onEnable();
    void onDisable();
    void onEstop();
    void requestAngles();

private:
    void buildUi();
    void setCurrentAngleValue(int axis, double degrees);
    void setPerAxisButtonsEnabled(bool enabled);

    RpcClient *rpc_;

    // Per-axis widgets
    QList<QDoubleSpinBox*> target_spins_;
    QList<QLabel*>         current_labels_;
    QList<QPushButton*>    axis_home_buttons_;
    QList<QPushButton*>    axis_move_buttons_;

    QTimer *poll_timer_;
    bool    angle_request_in_flight_{false};

    QPushButton *btn_enable_;
    QPushButton *btn_disable_;
    QPushButton *btn_set_;
    QPushButton *btn_estop_;
    QPushButton *btn_home_;
    QLabel      *status_label_;
};

#endif // ARMWIDGET_H
