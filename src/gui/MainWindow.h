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
class PiperWidget;
class UGVWidget;
class AirportWidget;
class DoorWidget;
class GripperWidget;
class MeshMapWidget;
class DroneWidget;
class MapWidget;
class CalibWidget;
class TeachWidget;
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
    void onVideoSourceToggled();       // RGB <-> colorized depth
    void pollDetections();
    // Moves the single CameraWidget between the dashboard slot and Tab4's
    // camera dock when the active tab changes. The two tabs never show
    // video at the same time.
    void onTabChanged(int index);

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
    QWidget      *dashboard_tab_ = nullptr;       // pointer for tab compare
    QSplitter    *dash_left_splitter_ = nullptr;  // owner when on dashboard
    LogWidget    *log_widget_;
    MapWidget    *map_widget_ = nullptr;          // 无人机地图 (below camera)
    ArmWidget    *arm_widget_;
    PiperWidget  *piper_widget_ = nullptr;   // gen-2 arm widget (shown when backend=piper)
    UGVWidget    *ugv_widget_;
    AirportWidget *airport_widget_;
    DoorWidget    *door_widget_;      // 舱门 / 停机坪 (proc_door, RS485)
    GripperWidget *gripper_widget_;
    MeshMapWidget *mesh_widget_;
    DroneWidget   *drone_widget_;
    CalibWidget   *calib_widget_   = nullptr;   // hand-eye calibration panel
    TeachWidget   *teach_widget_   = nullptr;   // record / replay teach paths

    Tab2CommConfig  *tab2_;
    Tab3Help        *tab3_;
    Tab4TaskConfig  *tab4_;
    NpuWidget       *npu_widget_;

    // Connection controls
    QLineEdit    *host_edit_;
    QSpinBox     *rpc_port_spin_;
    QSpinBox     *video_port_spin_;
    QRadioButton *video_enable_radio_;
    QPushButton  *btn_video_source_;   // toggles RGB ⇄ colorized depth
    bool          video_source_depth_{false};
    QPushButton  *btn_connect_;
    QPushButton  *btn_disconnect_;
    QLabel       *led_label_;

    // Status bar
    QLabel       *status_label_;
    QLabel       *fps_label_;

    // NPU detection polling (drives detection overlay on CameraWidget)
    QTimer       *det_timer_{nullptr};
};

#endif // MAINWINDOW_H
