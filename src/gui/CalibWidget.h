#ifndef CALIBWIDGET_H
#define CALIBWIDGET_H

#include <QGroupBox>
#include <QVector>
#include <QJsonObject>

class RpcClient;
class QPushButton;
class QLabel;
class QComboBox;
class QSpinBox;
class QPlainTextEdit;

// CalibWidget — hand-eye calibration between the RK3588-side RealSense
// and the Piper arm flange.
//
// Workflow (manual, operator-driven):
//   1. Pick mode: eye-in-hand (camera mounted on the wrist) or eye-to-base
//      (camera fixed on the chassis).
//   2. Jog the arm via the existing ArmWidget/PiperWidget to a pose where
//      the ArUco marker is visible.
//   3. Click "捕获样本" → the widget reads `arm.get_pose` and the marker's
//      camera-frame pose (from `npu.get_detections` when an ArUco strategy
//      is active) and stacks them as one sample.
//   4. Repeat ≥4 times across diverse orientations.
//   5. Click "求解" → sends the sample list to the gateway via
//      `calib.solve_hand_eye` (Tsai-Lenz on the backend). Reply contains
//      the 4×4 transform.
//   6. Click "应用到 RK3588" → pushes the result via `calib.set_hand_eye`
//      so proc_grasp can use it directly. The local copy is also persisted
//      in QSettings as a fallback.
class CalibWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit CalibWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onCapture();
    void onSolve();
    void onApply();
    void onReset();
    void onExportJson();

private:
    void buildUi();
    void updateSampleSummary();
    void appendLog(const QString &line);

    RpcClient *rpc_;

    QComboBox      *mode_combo_      = nullptr;   // eye-in-hand / eye-to-base
    QSpinBox       *aruco_id_spin_   = nullptr;
    QPushButton    *btn_capture_     = nullptr;
    QPushButton    *btn_solve_       = nullptr;
    QPushButton    *btn_apply_       = nullptr;
    QPushButton    *btn_reset_       = nullptr;
    QPushButton    *btn_export_      = nullptr;
    QLabel         *count_label_     = nullptr;
    QLabel         *result_label_    = nullptr;
    QPlainTextEdit *log_view_        = nullptr;

    // One row per captured sample. Stores both the arm-side and camera-
    // side observations so the solver can compute A_i / B_i pairs.
    struct Sample {
        QVector<double> arm_pose6;     // [x, y, z, rx, ry, rz] mm/deg, end effector in base frame
        QVector<double> marker_pose6;  // [x, y, z, rx, ry, rz] mm/deg, marker in camera frame
        int             aruco_id = 0;
    };
    QVector<Sample> samples_;

    // Result of the last solve: 4×4 row-major, identity if not solved yet.
    double last_T_[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool   has_result_ = false;
};

#endif // CALIBWIDGET_H
