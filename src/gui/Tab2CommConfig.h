#ifndef TAB2COMMCONFIG_H
#define TAB2COMMCONFIG_H

#include <QWidget>

class RpcClient;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QSettings;

class Tab2CommConfig : public QWidget {
    Q_OBJECT
public:
    explicit Tab2CommConfig(RpcClient *rpc, QWidget *parent = nullptr);

    // Called by MainWindow when connection params change
    void setConnectionParams(const QString &host, quint16 rpcPort, quint16 videoPort);

    // Persist / restore all interface-config UI fields.
    void loadConfig(QSettings &s);
    void saveConfig(QSettings &s) const;

private slots:
    void onApplyCan1();
    void onApplyCan3();
    void onApplyCan4();
    void onApplySerial();
    void onApplyRelay();
    void onApplyEthernet();

private:
    void buildUi();
    void applyCanConfig(const QString &device, QSpinBox *bitrate_spin);

    RpcClient  *rpc_;

    // CAN
    QSpinBox   *can1_bitrate_spin_;
    QSpinBox   *can3_bitrate_spin_;
    QSpinBox   *can4_bitrate_spin_;

    // Serial
    QLineEdit  *serial_device_edit_;
    QComboBox  *serial_baud_combo_;

    // Relay
    QSpinBox   *relay_gpio_spin_;
    QComboBox  *relay_active_combo_;

    // Ethernet
    QLineEdit  *eth_host_edit_;
    QSpinBox   *eth_rpc_port_spin_;
    QSpinBox   *eth_video_port_spin_;
};

#endif // TAB2COMMCONFIG_H
