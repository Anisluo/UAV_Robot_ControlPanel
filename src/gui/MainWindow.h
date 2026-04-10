#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class RpcClient;
class VideoClient;
class MeshPinger;
class CameraWidget;
class LogWidget;
class ArmWidget;
class UGVWidget;
class AirportWidget;
class GripperWidget;
class MeshMapWidget;
class DroneWidget;
class Tab2CommConfig;
class Tab3Help;
class Tab4TaskConfig;
class NpuWidget;

class QTabWidget;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QRadioButton;
class QStatusBar;
class QSplitter;

class QCloseEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnect();
    void onDisconnect();
    void onRpcConnected();
    void onRpcDisconnected();
    void onFpsUpdated(double fps);
    void onLogMessage(const QString &msg);
    void onVideoEnabledToggled(bool checked);

private:
    void buildUi();
    QWidget* buildDashboardTab();
    QWidget* buildConnectionGroup();
    void setLedColor(const QString &color);

    // Persist / restore UI parameters via QSettings (INI file).
    void loadConfig();
    void saveConfig() const;

    // Core clients
    RpcClient    *rpc_client_;
    VideoClient  *video_client_;
    MeshPinger   *mesh_pinger_;

    // GUI widgets
    QTabWidget   *tab_widget_;
    CameraWidget *camera_widget_;
    LogWidget    *log_widget_;
    ArmWidget    *arm_widget_;
    UGVWidget    *ugv_widget_;
    AirportWidget *airport_widget_;
    GripperWidget *gripper_widget_;
    MeshMapWidget *mesh_widget_;
    DroneWidget   *drone_widget_;

    Tab2CommConfig  *tab2_;
    Tab3Help        *tab3_;
    Tab4TaskConfig  *tab4_;
    NpuWidget       *npu_widget_;

    // Connection controls
    QLineEdit    *host_edit_;
    QSpinBox     *rpc_port_spin_;
    QSpinBox     *video_port_spin_;
    QRadioButton *video_enable_radio_;
    QPushButton  *btn_connect_;
    QPushButton  *btn_disconnect_;
    QLabel       *led_label_;

    // Status bar
    QLabel       *status_label_;
    QLabel       *fps_label_;
};

#endif // MAINWINDOW_H
