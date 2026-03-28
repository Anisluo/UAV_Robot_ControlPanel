#include "ArmWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

ArmWidget::ArmWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("六轴机械臂 [CAN]", parent)
    , rpc_(rpc)
    , poll_timer_(new QTimer(this))
{
    buildUi();

    connect(poll_timer_, &QTimer::timeout, this, &ArmWidget::requestAngles);
    poll_timer_->start(500);
}

void ArmWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    // ── Header row ──────────────────────────────────────────────────────────
    auto *header = new QGridLayout;
    header->setSpacing(6);
    auto makeHdr = [&](const QString &text, int col, Qt::Alignment align = Qt::AlignCenter) {
        auto *l = new QLabel(text, this);
        l->setStyleSheet("color: #607d8b; font-size: 11px;");
        l->setAlignment(align);
        header->addWidget(l, 0, col);
    };
    makeHdr("轴",       0, Qt::AlignRight | Qt::AlignVCenter);
    makeHdr("目标角度", 1);
    makeHdr("当前角度", 3);
    makeHdr("回零安全位", 5);
    mainLayout->addLayout(header);

    // ── Joint rows ───────────────────────────────────────────────────────────
    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    const QStringList names = {"J1", "J2", "J3", "J4", "J5", "J6"};
    const double defaultSafe[6] = {0.0, 45.0, 0.0, 0.0, 0.0, 0.0};

    for (int i = 0; i < 6; ++i) {
        // axis label
        auto *axLabel = new QLabel(names[i], this);
        axLabel->setFixedWidth(24);
        axLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        axLabel->setStyleSheet("font-family: Consolas; font-weight: bold; color: #00c8d7;");

        // target angle spinbox
        auto *targetSpin = new QDoubleSpinBox(this);
        targetSpin->setRange(-360.0, 360.0);
        targetSpin->setSingleStep(1.0);
        targetSpin->setDecimals(1);
        targetSpin->setValue(0.0);
        targetSpin->setFixedWidth(90);

        auto *degLabel1 = new QLabel("°", this);
        degLabel1->setFixedWidth(12);

        // current angle label
        auto *curLabel = new QLabel("--.-°", this);
        curLabel->setFixedWidth(72);
        curLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        curLabel->setStyleSheet("color: #9fb3c8; font-family: Consolas;");

        auto *degLabel2 = new QLabel("°", this);
        degLabel2->setFixedWidth(12);

        // home safe position spinbox
        auto *safeSpin = new QDoubleSpinBox(this);
        safeSpin->setRange(-360.0, 360.0);
        safeSpin->setSingleStep(1.0);
        safeSpin->setDecimals(1);
        safeSpin->setValue(defaultSafe[i]);
        safeSpin->setFixedWidth(90);

        auto *degLabel3 = new QLabel("°", this);
        degLabel3->setFixedWidth(12);

        target_spins_.append(targetSpin);
        current_labels_.append(curLabel);
        safe_spins_.append(safeSpin);

        grid->addWidget(axLabel,   i, 0);
        grid->addWidget(targetSpin, i, 1);
        grid->addWidget(degLabel1, i, 2);
        grid->addWidget(curLabel,  i, 3);
        grid->addWidget(degLabel2, i, 4);
        grid->addWidget(safeSpin,  i, 5);
        grid->addWidget(degLabel3, i, 6);
    }

    grid->setColumnStretch(7, 1);
    mainLayout->addLayout(grid);

    // ── Button row ───────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    btn_enable_  = new QPushButton("使能",   this);
    btn_disable_ = new QPushButton("失能",   this);
    btn_set_     = new QPushButton("设置",   this);
    btn_estop_   = new QPushButton("急停",   this);
    btn_home_    = new QPushButton("回零",   this);

    for (auto *b : {btn_enable_, btn_disable_, btn_set_, btn_estop_, btn_home_})
        b->setFixedHeight(28);

    btn_estop_->setStyleSheet(
        "QPushButton { background: #c0392b; color: white; font-weight: bold; }"
        "QPushButton:hover { background: #e74c3c; }"
        "QPushButton:pressed { background: #962d22; }"
    );

    btnRow->addWidget(btn_enable_);
    btnRow->addWidget(btn_disable_);
    btnRow->addWidget(btn_set_);
    btnRow->addWidget(btn_estop_);
    btnRow->addWidget(btn_home_);
    btnRow->addStretch();

    mainLayout->addLayout(btnRow);

    // ── Status label ─────────────────────────────────────────────────────────
    status_label_ = new QLabel("就绪", this);
    status_label_->setStyleSheet("color: #8aa1b4;");
    mainLayout->addWidget(status_label_);

    connect(btn_enable_,  &QPushButton::clicked, this, &ArmWidget::onEnable);
    connect(btn_disable_, &QPushButton::clicked, this, &ArmWidget::onDisable);
    connect(btn_set_,     &QPushButton::clicked, this, &ArmWidget::onSet);
    connect(btn_estop_,   &QPushButton::clicked, this, &ArmWidget::onEstop);
    connect(btn_home_,    &QPushButton::clicked, this, &ArmWidget::onHome);
}

// Send target angles to arm
void ArmWidget::onSet()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonArray joints;
    for (int i = 0; i < 6; ++i)
        joints.append(target_spins_[i]->value());
    QJsonObject params;
    params[Protocol::Fields::JOINTS] = joints;
    rpc_->call(Protocol::Methods::ARM_SET_JOINTS, params,
               [this](QJsonObject result) {
        const bool ok = result.value("ok").toBool(false);
        status_label_->setText(ok ? "设置完成" : "设置失败: " + result.value("error").toString());
    });
}

// Sequential home: each axis homes then moves to its safe position
void ArmWidget::onHome()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    status_label_->setText("回零中...");

    QJsonArray safeDegrees;
    for (int i = 0; i < 6; ++i)
        safeDegrees.append(safe_spins_[i]->value());

    QJsonObject params;
    params["action"]       = QString("home_seq");
    params["safe_degrees"] = safeDegrees;
    // keep j2_safe_deg for backward compat with server
    params["j2_safe_deg"]  = safe_spins_[1]->value();

    rpc_->call(Protocol::Methods::ARM_HOME, params,
               [this](QJsonObject result) {
        const bool ok = result.value("ok").toBool(false);
        if (!ok) {
            status_label_->setText("回零失败: " + result.value("error").toString("未知错误"));
            return;
        }
        status_label_->setText("回零完成");
    });
}

void ArmWidget::onEnable()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params["enable"] = true;
    rpc_->call("arm.enable", params,
               [this](QJsonObject result) {
        status_label_->setText(result.value("ok").toBool(false) ? "使能成功" : "使能失败");
    });
}

void ArmWidget::onDisable()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    QJsonObject params;
    params["enable"] = false;
    rpc_->call("arm.enable", params,
               [this](QJsonObject result) {
        status_label_->setText(result.value("ok").toBool(false) ? "失能成功" : "失能失败");
    });
}

void ArmWidget::onEstop()
{
    if (!rpc_ || !rpc_->isConnected()) return;
    rpc_->call("arm.estop", QJsonObject{},
               [this](QJsonObject result) {
        status_label_->setText(result.value("ok").toBool(false) ? "急停完成" : "急停失败");
    });
}

void ArmWidget::requestAngles()
{
    if (!rpc_ || !rpc_->isConnected() || angle_request_in_flight_) return;

    angle_request_in_flight_ = true;
    rpc_->call(Protocol::Methods::ARM_GET_ANGLES, QJsonObject{},
               [this](QJsonObject result) {
        angle_request_in_flight_ = false;
        const bool ok = result.value("ok").toBool(false);
        if (!ok) return;

        const QJsonArray angles = result.value(Protocol::Fields::ANGLES).toArray();
        if (angles.size() < 6) return;
        for (int i = 0; i < 6; ++i)
            setCurrentAngleValue(i, angles.at(i).toDouble());
    });
}

void ArmWidget::setCurrentAngleValue(int axis, double degrees)
{
    if (axis < 0 || axis >= current_labels_.size()) return;
    current_labels_[axis]->setText(QString("%1°").arg(degrees, 6, 'f', 1));
}
