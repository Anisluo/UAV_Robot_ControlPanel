#include "Tab4TaskConfig.h"
#include "LogWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QJsonObject>
#include <QFrame>

Tab4TaskConfig::Tab4TaskConfig(RpcClient *rpc, QWidget *parent)
    : QWidget(parent)
    , rpc_(rpc)
{
    buildUi();
}

void Tab4TaskConfig::buildUi()
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(6, 6, 6, 6);
    outerLayout->setSpacing(6);

    // ── Main upper splitter (3D view | task panel) ────────────────────────
    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    mainSplitter->addWidget(build3DPlaceholder());
    mainSplitter->addWidget(buildTaskPanel());
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({700, 350});

    outerLayout->addWidget(mainSplitter, 3);

    // ── Bottom: log ───────────────────────────────────────────────────────
    log_widget_ = new LogWidget(this);
    log_widget_->setMinimumHeight(120);
    outerLayout->addWidget(log_widget_, 1);
}

QWidget* Tab4TaskConfig::build3DPlaceholder()
{
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(
        "QFrame { background-color: #1a1b2e; border: 1px solid #353650; border-radius: 4px; }");

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *placeholder = new QLabel(this);
    placeholder->setText(
        "<div style='text-align:center; color:#555770;'>"
        "<div style='font-size:48px;'>⬡</div>"
        "<div style='font-size:14px; margin-top:12px;'>3D 场景视图</div>"
        "<div style='font-size:11px; margin-top:6px;'>小车 + 机械臂 + 平台目标云图</div>"
        "<div style='font-size:10px; margin-top:4px;'>(待实现)</div>"
        "</div>");
    placeholder->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholder);

    return frame;
}

QWidget* Tab4TaskConfig::buildTaskPanel()
{
    auto *grp = new QGroupBox("任务配置", this);
    auto *layout = new QVBoxLayout(grp);
    layout->setSpacing(8);
    layout->setContentsMargins(8, 18, 8, 8);

    layout->addWidget(new QLabel("可用任务列表:", grp));

    task_list_ = new QListWidget(grp);
    task_list_->setAlternatingRowColors(true);
    task_list_->setStyleSheet(
        "QListWidget { background: #ffffff; color: #1a1a2e; }"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:alternate { background: #f0f2f8; }"
        "QListWidget::item:selected { background: #1565c0; color: #ffffff; }"
        "QListWidget::item:hover:!selected { background: #dce4f5; }");

    // Pre-defined task entries
    auto addTask = [&](const QString &name, const QString &desc, const QString &key) {
        auto *item = new QListWidgetItem(
            QString("%1\n  %2").arg(name).arg(desc), task_list_);
        item->setData(Qt::UserRole, key);
        item->setSizeHint(QSize(0, 48));
    };

    addTask("电池拾取任务",  "识别方形电池 → 机械臂夹取 → 放入电池槽", "battery_pick");
    addTask("机械臂回零",    "六轴机械臂回零位",                        "arm_home");
    addTask("平台锁定",      "锁定承载平台",                             "platform_lock");
    addTask("相机标定",      "执行相机内参标定流程",                     "camera_calib");

    task_list_->setCurrentRow(0);
    layout->addWidget(task_list_, 1);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btn_start_ = new QPushButton("执行选中任务", grp);
    btn_stop_  = new QPushButton("停止",         grp);
    btn_reset_ = new QPushButton("复位",         grp);
    btn_start_->setFixedHeight(30);
    btn_stop_->setFixedHeight(30);
    btn_reset_->setFixedHeight(30);
    btn_stop_->setEnabled(false);
    btnRow->addWidget(btn_start_);
    btnRow->addWidget(btn_stop_);
    btnRow->addWidget(btn_reset_);
    layout->addLayout(btnRow);

    // ── Status ────────────────────────────────────────────────────────────
    task_status_label_ = new QLabel("就绪", grp);
    task_status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    layout->addWidget(task_status_label_);

    connect(btn_start_, &QPushButton::clicked, this, &Tab4TaskConfig::onStartTask);
    connect(btn_stop_,  &QPushButton::clicked, this, &Tab4TaskConfig::onStopTask);
    connect(btn_reset_, &QPushButton::clicked, this, &Tab4TaskConfig::onResetTask);

    return grp;
}

void Tab4TaskConfig::onStartTask()
{
    auto *cur = task_list_->currentItem();
    if (!cur) return;

    QString key = cur->data(Qt::UserRole).toString();
    QJsonObject params;
    params[Protocol::Fields::TASK_NAME] = key;

    rpc_->call(Protocol::Methods::TASK_START, params,
        [this](QJsonObject r) { onStartResult(r); });

    btn_start_->setEnabled(false);
    btn_stop_->setEnabled(true);
    task_status_label_->setText("任务执行中...");
    task_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");
    log_widget_->appendLog("INFO", QString("[任务] 已发送启动指令: %1")
        .arg(task_list_->currentItem()->text().split('\n').first()));
}

void Tab4TaskConfig::onStopTask()
{
    // Immediately re-enable start so the user can restart without waiting for RPC reply
    btn_start_->setEnabled(true);
    btn_stop_->setEnabled(false);
    task_status_label_->setText("正在停止...");
    task_status_label_->setStyleSheet("font-family: Consolas; color: #ff9800;");

    rpc_->call(Protocol::Methods::TASK_STOP, QJsonObject{},
        [this](QJsonObject r) { onStopResult(r); });
    log_widget_->appendLog("WARN", "[任务] 已发送停止指令");
}

void Tab4TaskConfig::onResetTask()
{
    rpc_->call(Protocol::Methods::TASK_RESET, QJsonObject{},
        [this](QJsonObject) {
            task_status_label_->setText("已复位");
            task_status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
            btn_start_->setEnabled(true);
            btn_stop_->setEnabled(false);
        });
    log_widget_->appendLog("INFO", "[任务] 已发送复位指令");
}

void Tab4TaskConfig::onStartResult(QJsonObject result)
{
    bool ok = result.value("ok").toBool(false);
    if (ok) {
        task_status_label_->setText("任务运行中");
        task_status_label_->setStyleSheet("font-family: Consolas; color: #4caf50;");
        log_widget_->appendLog("INFO", "[任务] 启动成功");
    } else {
        task_status_label_->setText("启动失败: " + result.value("error").toString());
        task_status_label_->setStyleSheet("font-family: Consolas; color: #f44336;");
        btn_start_->setEnabled(true);
        btn_stop_->setEnabled(false);
        log_widget_->appendLog("ERROR", "[任务] 启动失败");
    }
}

void Tab4TaskConfig::onStopResult(QJsonObject result)
{
    Q_UNUSED(result)
    btn_start_->setEnabled(true);
    btn_stop_->setEnabled(false);
    task_status_label_->setText("任务已停止");
    task_status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    log_widget_->appendLog("INFO", "[任务] 已停止");
}

void Tab4TaskConfig::appendLog(const QString &level, const QString &msg)
{
    log_widget_->appendLog(level, msg);
}

void Tab4TaskConfig::setConnectionParams(const QString &host, quint16 rpc_port, quint16 vid_port)
{
    Q_UNUSED(host) Q_UNUSED(rpc_port) Q_UNUSED(vid_port)
    // Reserved for future per-tab connection display
}
