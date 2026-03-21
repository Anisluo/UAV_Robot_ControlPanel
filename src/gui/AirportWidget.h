#ifndef AIRPORTWIDGET_H
#define AIRPORTWIDGET_H

#include <QGroupBox>
#include <QList>

class RpcClient;
class QSlider;
class QSpinBox;
class QPushButton;

class AirportWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit AirportWidget(RpcClient *rpc, QWidget *parent = nullptr);

private slots:
    void onRailSliderChanged(int rail, int value);
    void onRailSpinChanged(int rail, int value);
    void onSetRail(int rail);
    void onAllHome();

private:
    void buildUi();

    RpcClient          *rpc_;
    QList<QSlider*>     rail_sliders_;
    QList<QSpinBox*>    rail_spins_;
    QList<QPushButton*> rail_set_btns_;
    QPushButton        *btn_all_home_;
};

#endif // AIRPORTWIDGET_H
