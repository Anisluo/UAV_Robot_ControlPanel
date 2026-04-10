#ifndef ARMWIDGET_H
#define ARMWIDGET_H

#include <QGroupBox>
#include <QList>

class RpcClient;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QTimer;
class QLabel;
class QSettings;

class ArmWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit ArmWidget(RpcClient *rpc, QWidget *parent = nullptr);

    void loadConfig(QSettings &s);
    void saveConfig(QSettings &s) const;

private slots:
    void onHome();
    void onHomeAxis();
    void onMoveAxis();
    void onEstop();
    void onSpeedsChanged();
    void requestAngles();

public slots:
    // Push the current move/zero RPM values to proc_arm.
    // Called automatically when the user edits the spin boxes and once on
    // (re)connect, so the backend always reflects what the GUI shows.
    void pushSpeeds();

private:
    void buildUi();
    void setCurrentAngleValue(int axis, double degrees);
    void setPerAxisButtonsEnabled(bool enabled);

    RpcClient *rpc_;

    // Per-axis widgets
    QList<QDoubleSpinBox*> target_spins_;   // 目标位置（移动用）
    QList<QDoubleSpinBox*> safe_spins_;     // 安全位置（回零后移动到此）
    QList<QLabel*>         current_labels_;
    QList<QPushButton*>    axis_home_buttons_;
    QList<QPushButton*>    axis_move_buttons_;

    QTimer *poll_timer_;
    bool    angle_request_in_flight_{false};

    QPushButton *btn_estop_;
    QPushButton *btn_home_;
    QSpinBox    *zero_dwell_spin_;   // 到零点后停留时间 (ms)，再移动到安全位
    QSpinBox    *move_speed_spin_;   // 移动速度 (RPM) - 推送给 proc_arm
    QSpinBox    *zero_speed_spin_;   // 归零速度 (RPM) - 推送给 proc_arm
    QLabel      *status_label_;
};

#endif // ARMWIDGET_H
