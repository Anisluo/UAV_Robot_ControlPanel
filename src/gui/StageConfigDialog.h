#ifndef STAGECONFIGDIALOG_H
#define STAGECONFIGDIALOG_H

#include <QDialog>
#include <QVector>
#include "TaskStep.h"

class RpcClient;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QPushButton;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;

// Modal editor for one stage's TaskStep script. Opened when the operator
// clicks the ⚙ gear on a TaskFlowWidget card.
//
// Layout:
//   ┌───────────────────────────────────────────────────────────┐
//   │  Stage: <CN title> (id)                                   │
//   ├───────────────────────────┬───────────────────────────────┤
//   │  ▢ Step list              │  Edit panel (stacked, per type)│
//   │  1  类型  参数summary       │                               │
//   │  2  ...                    │  param1: [____]               │
//   │  3                          │  param2: [____]               │
//   │                            │  …                            │
//   │  [+ 加]  [✗ 删]  [↑] [↓]   │                               │
//   │  [📍 录当前关节]            │                               │
//   ├───────────────────────────┴───────────────────────────────┤
//   │  [保存]  [取消]                                            │
//   └───────────────────────────────────────────────────────────┘
class StageConfigDialog : public QDialog {
    Q_OBJECT
public:
    StageConfigDialog(const QString &stage_id,
                       const QString &stage_title,
                       const QVector<TaskStep> &existing_steps,
                       RpcClient *rpc,         // used by "录当前关节" to query arm.get_angles
                       QWidget *parent = nullptr);

    QVector<TaskStep> steps() const { return steps_; }

signals:
    // 保存 按钮触发. 不再走 QDialog::accept 关窗口的路 — 直接交给 Tab4
    // 立即写 JSON, 对话框保持打开方便操作员继续编辑/验证.
    void saveStage(const QVector<TaskStep> &steps);

private slots:
    void onAddStep();
    void onRemoveStep();
    void onMoveUp();
    void onMoveDown();
    void onRowChanged(int row);
    void onRecordCurrentJoints();
    void onParamChanged();       // any editor field → write back into steps_[cur_row]
    void onExecuteCurrentStep(); // 用当前编辑器参数立刻在真机上执行选中步骤

private:
    void buildUi();
    void buildEditPanels();
    void refreshList();
    void showRow(int row);       // switch stacked editor + populate fields
    void readEditorsTo(TaskStep &s);   // copy edit-panel values into the step

    QString    stage_id_;
    QString    stage_title_;
    RpcClient *rpc_   = nullptr;
    QVector<TaskStep> steps_;
    int        cur_row_ = -1;

    // left side
    QListWidget *list_      = nullptr;
    QPushButton *btn_add_   = nullptr;
    QComboBox   *cmb_add_   = nullptr;
    QPushButton *btn_del_   = nullptr;
    QPushButton *btn_up_    = nullptr;
    QPushButton *btn_dn_    = nullptr;
    QPushButton *btn_record_= nullptr;
    QPushButton *btn_execute_ = nullptr;   // 底部 ▶ 执行 — 立刻跑选中步骤

    // right side: stacked editors per type, indexed by int(StepType)
    QStackedWidget *editor_stack_ = nullptr;
    QLabel         *editor_title_ = nullptr;
    class QLineEdit *step_note_edit_ = nullptr;   // 备注 (stored in TaskStep.label)

    // MOVE_JOINTS
    QDoubleSpinBox *mj_j_[6]   = {nullptr};
    QSpinBox       *mj_speed_  = nullptr;

    // MOVE_CARTESIAN
    QDoubleSpinBox *mc_x_      = nullptr;
    QDoubleSpinBox *mc_y_      = nullptr;
    QDoubleSpinBox *mc_z_      = nullptr;
    QDoubleSpinBox *mc_rx_     = nullptr;
    QDoubleSpinBox *mc_ry_     = nullptr;
    QDoubleSpinBox *mc_rz_     = nullptr;
    QComboBox      *mc_mode_   = nullptr;

    // GRIPPER
    QDoubleSpinBox *gr_angle_  = nullptr;
    QSpinBox       *gr_force_  = nullptr;

    // AIRPORT_RAIL — rail lock/release/rail2 actions, two stop modes
    QComboBox      *ar_action_     = nullptr;   // lock/release/rail2_fwd/rail2_back
    QComboBox      *ar_stop_mode_  = nullptr;   // stall / distance
    QSpinBox       *ar_speed_      = nullptr;   // rpm
    QDoubleSpinBox *ar_distance_   = nullptr;   // mm (distance mode only)
    QSpinBox       *ar_max_ms_     = nullptr;   // GUI upper bound

    // AIRPORT_GRIPPER
    QCheckBox      *ag_open_   = nullptr;

    // WAIT_DETECT_UAV / WAIT_DETECT_BAT (share schema)
    QCheckBox      *wd_present_= nullptr;
    QSpinBox       *wd_timeout_= nullptr;

    QCheckBox      *wd_bat_present_= nullptr;
    QSpinBox       *wd_bat_timeout_= nullptr;

    // DWELL
    QSpinBox       *dw_ms_     = nullptr;

    // FIX_POINT (定点跟踪)
    QDoubleSpinBox *fp_x_      = nullptr;
    QDoubleSpinBox *fp_y_      = nullptr;
    QDoubleSpinBox *fp_z_      = nullptr;
    QDoubleSpinBox *fp_rx_     = nullptr;
    QDoubleSpinBox *fp_ry_     = nullptr;
    QDoubleSpinBox *fp_rz_     = nullptr;
    QSpinBox       *fp_duration_ms_ = nullptr;
    QPushButton    *btn_fp_record_  = nullptr;   // "录当前 TCP 为目标点"
};

#endif // STAGECONFIGDIALOG_H
