#ifndef NPUWIDGET_H
#define NPUWIDGET_H

#include <QWidget>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

class RpcClient;
class QComboBox;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class QTableWidget;
class QTimer;
class QSettings;

// NPU Recognition Strategy Control Widget
// Provides: strategy selection, start/stop, threshold config,
//           live detection result table updated by polling.
class NpuWidget : public QWidget {
    Q_OBJECT
public:
    explicit NpuWidget(RpcClient *rpc, QWidget *parent = nullptr);

    void loadConfig(QSettings &s);
    void saveConfig(QSettings &s) const;

private slots:
    void onStart();
    void onStop();
    void onSetStrategy();
    void onSetThreshold();
    void onPollDetections();

    void onStartResult(QJsonObject result);
    void onStopResult(QJsonObject result);
    void onStrategyResult(QJsonObject result);
    void onDetectionsResult(QJsonObject result);

private:
    void buildUi();
    void setStatus(const QString &text, const QString &color = "#dde1f0");
    void updateDetectionTable(const QJsonArray &dets);

    RpcClient       *rpc_;

    QComboBox       *strategy_combo_;
    QDoubleSpinBox  *threshold_spin_;
    QPushButton     *btn_start_;
    QPushButton     *btn_stop_;
    QPushButton     *btn_apply_;
    QLabel          *status_label_;
    QTableWidget    *det_table_;
    QTimer          *poll_timer_;
};

#endif // NPUWIDGET_H
