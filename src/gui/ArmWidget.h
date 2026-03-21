#ifndef ARMWIDGET_H
#define ARMWIDGET_H

#include <QGroupBox>
#include <QList>

class RpcClient;
class QSlider;
class QDoubleSpinBox;
class QPushButton;
class QTimer;

class ArmWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit ArmWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onSliderChanged(int axis, int value);
    void onSpinboxChanged(int axis, double value);
    void sendJoints();
    void onHome();
    void onEnable();
    void onDisable();

private:
    void buildUi();
    void setJointValue(int axis, double degrees, bool blockSignals);
    double jointValue(int axis) const;

    RpcClient         *rpc_;
    QList<QSlider*>    sliders_;
    QList<QDoubleSpinBox*> spinboxes_;
    QTimer            *debounce_timer_;

    QPushButton *btn_home_;
    QPushButton *btn_enable_;
    QPushButton *btn_disable_;
};

#endif // ARMWIDGET_H
