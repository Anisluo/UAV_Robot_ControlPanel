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
    void onEnable();
    void onDisable();
    void onEstop();
    void requestAngles();

private:
    void buildUi();
    void setCurrentAngleValue(int axis, double degrees);

    RpcClient *rpc_;

    // Per-axis: target angle spinbox, current angle label, home-safe-pos spinbox
    QList<QDoubleSpinBox*> target_spins_;
    QList<QLabel*>         current_labels_;
    QList<QDoubleSpinBox*> safe_spins_;

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
