#include "NpuWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QSettings>

NpuWidget::NpuWidget(RpcClient *rpc, QWidget *parent)
    : QWidget(parent)
    , rpc_(rpc)
{
    buildUi();

    // Poll for detections every 500 ms while running
    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(500);
    connect(poll_timer_, &QTimer::timeout, this, &NpuWidget::onPollDetections);
}

void NpuWidget::buildUi()
{
    auto *grp = new QGroupBox("NPU 识别策略", this);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(grp);

    auto *layout = new QVBoxLayout(grp);
    layout->setSpacing(6);
    layout->setContentsMargins(8, 18, 8, 8);

    // ── Strategy row ──────────────────────────────────────────────────────
    auto *stratRow = new QHBoxLayout;
    stratRow->addWidget(new QLabel("策略:", grp));
    strategy_combo_ = new QComboBox(grp);
    strategy_combo_->addItem("方形电池检测 (default)",  0);
    strategy_combo_->addItem("电池 V2 (battery_v2)",    1);
    strategy_combo_->addItem("自定义 (custom)",          2);
    strategy_combo_->addItem("人脸追踪 (face)",          3);
    strategy_combo_->addItem("御三电池识别 (Mavic3 battery)", 4);
    stratRow->addWidget(strategy_combo_, 1);

    btn_apply_ = new QPushButton("应用", grp);
    btn_apply_->setFixedWidth(52);
    stratRow->addWidget(btn_apply_);
    layout->addLayout(stratRow);

    // ── Threshold row ─────────────────────────────────────────────────────
    auto *thrRow = new QHBoxLayout;
    thrRow->addWidget(new QLabel("置信度阈值:", grp));
    threshold_spin_ = new QDoubleSpinBox(grp);
    threshold_spin_->setRange(0.0, 1.0);
    threshold_spin_->setSingleStep(0.05);
    threshold_spin_->setValue(0.50);
    threshold_spin_->setDecimals(2);
    threshold_spin_->setFixedWidth(70);
    thrRow->addWidget(threshold_spin_);
    thrRow->addStretch();
    layout->addLayout(thrRow);

    // ── Control buttons ───────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btn_start_ = new QPushButton("启动识别", grp);
    btn_stop_  = new QPushButton("停止识别", grp);
    btn_start_->setFixedHeight(28);
    btn_stop_->setFixedHeight(28);
    btn_stop_->setEnabled(false);
    btnRow->addWidget(btn_start_);
    btnRow->addWidget(btn_stop_);
    layout->addLayout(btnRow);

    // ── Status ────────────────────────────────────────────────────────────
    status_label_ = new QLabel("就绪", grp);
    status_label_->setStyleSheet("font-family: Consolas; color: #dde1f0;");
    layout->addWidget(status_label_);

    // ── Detection results table ───────────────────────────────────────────
    det_table_ = new QTableWidget(0, 6, grp);
    det_table_->setHorizontalHeaderLabels({"类别", "置信度", "X(mm)", "Y(mm)", "Z(mm)", "像素框"});
    det_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    det_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    det_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    det_table_->setMinimumHeight(120);
    layout->addWidget(det_table_);

    // ── Signals ───────────────────────────────────────────────────────────
    connect(btn_start_,  &QPushButton::clicked, this, &NpuWidget::onStart);
    connect(btn_stop_,   &QPushButton::clicked, this, &NpuWidget::onStop);
    connect(btn_apply_,  &QPushButton::clicked, this, &NpuWidget::onSetStrategy);
    connect(threshold_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onSetThreshold(); });
}

void NpuWidget::onStart()
{
    // First apply current strategy
    onSetStrategy();
    onSetThreshold();

    rpc_->call(Protocol::Methods::NPU_START, QJsonObject{},
        [this](QJsonObject r) { onStartResult(r); });
}

void NpuWidget::onStop()
{
    poll_timer_->stop();
    rpc_->call(Protocol::Methods::NPU_STOP, QJsonObject{},
        [this](QJsonObject r) { onStopResult(r); });
}

void NpuWidget::onSetStrategy()
{
    // "Apply" only switches the inference model / strategy. It never moves
    // the arm — arm-follow for face tracking is a separate task, started
    // explicitly by the user (e.g. task.start("face_track")), so that
    // debugging detection boxes does not depend on any actuator being up.
    int id = strategy_combo_->currentData().toInt();
    QJsonObject params;
    params[Protocol::Fields::STRATEGY_ID] = id;
    rpc_->call(Protocol::Methods::NPU_SET_STRATEGY, params,
        [this](QJsonObject r) { onStrategyResult(r); });
}

void NpuWidget::onSetThreshold()
{
    double v = threshold_spin_->value();
    QJsonObject params;
    params[Protocol::Fields::THRESHOLD] = v;
    rpc_->call(Protocol::Methods::NPU_SET_THRESHOLD, params,
        [](QJsonObject) {});
}

void NpuWidget::onPollDetections()
{
    rpc_->call(Protocol::Methods::NPU_GET_DETECTIONS, QJsonObject{},
        [this](QJsonObject r) { onDetectionsResult(r); });
}

void NpuWidget::onStartResult(QJsonObject result)
{
    Q_UNUSED(result)
    btn_start_->setEnabled(false);
    btn_stop_->setEnabled(true);
    setStatus("识别运行中...", "#4caf50");
    poll_timer_->start();
}

void NpuWidget::onStopResult(QJsonObject result)
{
    Q_UNUSED(result)
    btn_start_->setEnabled(true);
    btn_stop_->setEnabled(false);
    setStatus("识别已停止", "#ff9800");
}

void NpuWidget::onStrategyResult(QJsonObject result)
{
    Q_UNUSED(result)
    setStatus(QString("策略已切换: %1").arg(strategy_combo_->currentText()));
}

void NpuWidget::onDetectionsResult(QJsonObject result)
{
    QJsonArray dets = result.value(Protocol::Fields::DETECTIONS).toArray();
    updateDetectionTable(dets);

    quint64 fid = static_cast<quint64>(result.value(Protocol::Fields::FRAME_ID).toDouble());
    int n = result.value(Protocol::Fields::NUM_DETS).toInt(0);
    setStatus(QString("帧 %1 | 检测到 %2 个目标").arg(fid).arg(n));
}

void NpuWidget::updateDetectionTable(const QJsonArray &dets)
{
    det_table_->setRowCount(dets.size());
    for (int i = 0; i < dets.size(); ++i) {
        QJsonObject d = dets[i].toObject();
        det_table_->setItem(i, 0, new QTableWidgetItem(
            QString::number(d.value(Protocol::Fields::CLASS_ID).toInt())));
        det_table_->setItem(i, 1, new QTableWidgetItem(
            QString::number(d.value(Protocol::Fields::SCORE).toDouble(), 'f', 3)));
        det_table_->setItem(i, 2, new QTableWidgetItem(
            d.value(Protocol::Fields::HAS_XYZ).toBool()
                ? QString::number(d.value(Protocol::Fields::X_MM).toDouble(), 'f', 1)
                : "-"));
        det_table_->setItem(i, 3, new QTableWidgetItem(
            d.value(Protocol::Fields::HAS_XYZ).toBool()
                ? QString::number(d.value(Protocol::Fields::Y_MM).toDouble(), 'f', 1)
                : "-"));
        det_table_->setItem(i, 4, new QTableWidgetItem(
            d.value(Protocol::Fields::HAS_XYZ).toBool()
                ? QString::number(d.value(Protocol::Fields::Z_MM).toDouble(), 'f', 1)
                : "-"));
        // Pixel bbox as "x1,y1–x2,y2"
        QString bbox = QString("%1,%2–%3,%4")
            .arg(d.value(Protocol::Fields::X1).toDouble(), 0, 'f', 0)
            .arg(d.value(Protocol::Fields::Y1).toDouble(), 0, 'f', 0)
            .arg(d.value(Protocol::Fields::X2).toDouble(), 0, 'f', 0)
            .arg(d.value(Protocol::Fields::Y2).toDouble(), 0, 'f', 0);
        det_table_->setItem(i, 5, new QTableWidgetItem(bbox));
    }
}

void NpuWidget::setStatus(const QString &text, const QString &color)
{
    status_label_->setText(text);
    status_label_->setStyleSheet(
        QString("font-family: Consolas; color: %1;").arg(color));
}

void NpuWidget::loadConfig(QSettings &s)
{
    s.beginGroup("NpuWidget");
    if (strategy_combo_) {
        const QString name = s.value("strategy", strategy_combo_->currentText()).toString();
        const int idx = strategy_combo_->findText(name);
        if (idx >= 0) strategy_combo_->setCurrentIndex(idx);
    }
    if (threshold_spin_) {
        threshold_spin_->setValue(s.value("threshold", threshold_spin_->value()).toDouble());
    }
    s.endGroup();
}

void NpuWidget::saveConfig(QSettings &s) const
{
    s.beginGroup("NpuWidget");
    if (strategy_combo_) s.setValue("strategy", strategy_combo_->currentText());
    if (threshold_spin_) s.setValue("threshold", threshold_spin_->value());
    s.endGroup();
}
