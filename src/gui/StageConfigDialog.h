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

private slots:
    void onAddStep();
    void onRemoveStep();
    void onMoveUp();
    void onMoveDown();
    void onRowChanged(int row);
    void onRecordCurrentJoints();
    void onParamChanged();       // any editor field → write back into steps_[cur_row]

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

    // AIRPORT_RAIL
    QSpinBox       *ar_rail_   = nullptr;
    QDoubleSpinBox *ar_pos_    = nullptr;
    QSpinBox       *ar_speed_  = nullptr;

    // AIRPORT_RAIL_STALL  (runs until backend stall monitor stops the motor)
    QComboBox      *ars_action_  = nullptr;  // lock / release / rail2_fwd / rail2_back
    QSpinBox       *ars_speed_   = nullptr;  // rpm magnitude
    QSpinBox       *ars_max_ms_  = nullptr;  // GUI upper bound before advancing

    // AIRPORT_GRIPPER
    QCheckBox      *ag_open_   = nullptr;

    // WAIT_DETECT_UAV / WAIT_DETECT_BAT (share schema)
    QCheckBox      *wd_present_= nullptr;
    QSpinBox       *wd_timeout_= nullptr;

    QCheckBox      *wd_bat_present_= nullptr;
    QSpinBox       *wd_bat_timeout_= nullptr;

    // DWELL
    QSpinBox       *dw_ms_     = nullptr;
};

#endif // STAGECONFIGDIALOG_H
