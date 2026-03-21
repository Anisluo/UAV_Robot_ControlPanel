#include "AirportWidget.h"
#include "core/RpcClient.h"
#include "core/Protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>

AirportWidget::AirportWidget(RpcClient *rpc, QWidget *parent)
    : QGroupBox("机场平台 [CAN]", parent)
    , rpc_(rpc)
{
    buildUi();
}

void AirportWidget::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 18, 8, 8);

    auto *grid = new QGridLayout;
    grid->setSpacing(6);

    for (int i = 0; i < 3; ++i) {
        auto *label = new QLabel(QString("导轨%1").arg(i + 1), this);
        label->setStyleSheet("color: #00c8d7; font-family: Consolas;");
        label->setFixedWidth(55);

        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(0, 1000);
        slider->setValue(0);

        auto *spinbox = new QSpinBox(this);
        spinbox->setRange(0, 1000);
        spinbox->setSuffix(" mm");
        spinbox->setValue(0);
        spinbox->setFixedWidth(90);

        auto *setBtn = new QPushButton("设置", this);
        setBtn->setFixedWidth(50);
        setBtn->setFixedHeight(26);

        rail_sliders_.append(slider);
        rail_spins_.append(spinbox);
        rail_set_btns_.append(setBtn);

        int rail = i;
        connect(slider, &QSlider::valueChanged, this, [this, rail](int v) {
            onRailSliderChanged(rail, v);
        });
        connect(spinbox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, rail](int v) {
            onRailSpinChanged(rail, v);
        });
        connect(setBtn, &QPushButton::clicked, this, [this, rail]() {
            onSetRail(rail);
        });

        grid->addWidget(label,   i, 0);
        grid->addWidget(slider,  i, 1);
        grid->addWidget(spinbox, i, 2);
        grid->addWidget(setBtn,  i, 3);
    }

    grid->setColumnStretch(1, 1);
    mainLayout->addLayout(grid);

    // All Home button
    auto *btnRow = new QHBoxLayout;
    btn_all_home_ = new QPushButton("全部回零", this);
    btn_all_home_->setFixedHeight(28);
    btnRow->addWidget(btn_all_home_);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    connect(btn_all_home_, &QPushButton::clicked, this, &AirportWidget::onAllHome);
}

void AirportWidget::onRailSliderChanged(int rail, int value)
{
    bool blocked = rail_spins_[rail]->blockSignals(true);
    rail_spins_[rail]->setValue(value);
    rail_spins_[rail]->blockSignals(blocked);
}

void AirportWidget::onRailSpinChanged(int rail, int value)
{
    bool blocked = rail_sliders_[rail]->blockSignals(true);
    rail_sliders_[rail]->setValue(value);
    rail_sliders_[rail]->blockSignals(blocked);
}

void AirportWidget::onSetRail(int rail)
{
    if (!rpc_ || !rpc_->isConnected()) return;

    QJsonObject params;
    params[Protocol::Fields::RAIL]   = rail;
    params[Protocol::Fields::POS_MM] = static_cast<double>(rail_spins_[rail]->value());
    rpc_->call(Protocol::Methods::AIRPORT_SET_RAIL, params);
}

void AirportWidget::onAllHome()
{
    if (!rpc_ || !rpc_->isConnected()) return;

    for (int i = 0; i < 3; ++i) {
        QJsonObject params;
        params[Protocol::Fields::RAIL]   = i;
        params[Protocol::Fields::POS_MM] = 0.0;
        rpc_->call(Protocol::Methods::AIRPORT_SET_RAIL, params);
    }

    // Reset UI
    for (int i = 0; i < 3; ++i) {
        bool b1 = rail_sliders_[i]->blockSignals(true);
        bool b2 = rail_spins_[i]->blockSignals(true);
        rail_sliders_[i]->setValue(0);
        rail_spins_[i]->setValue(0);
        rail_sliders_[i]->blockSignals(b1);
        rail_spins_[i]->blockSignals(b2);
    }
}
