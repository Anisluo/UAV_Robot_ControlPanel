#ifndef TAB4TASKCONFIG_H
#define TAB4TASKCONFIG_H

#include <QWidget>
#include <QString>
#include <QJsonObject>

class RpcClient;
class LogWidget;
class QListWidget;
class QLabel;
class QPushButton;
class QSplitter;

// Task Configuration Tab
// Layout:
//   ── Horizontal Splitter ──────────────────────────────────────────────────
//   │  Left: 3D visualization placeholder (future: small car+arm+objects)   │
//   │  Right: task list + action buttons + task status                       │
//   ── Bottom: shared LogWidget ─────────────────────────────────────────────
class Tab4TaskConfig : public QWidget {
    Q_OBJECT
public:
    explicit Tab4TaskConfig(RpcClient *rpc, QWidget *parent = nullptr);

    void appendLog(const QString &level, const QString &msg);
    void setConnectionParams(const QString &host, quint16 rpc_port, quint16 vid_port);

private slots:
    void onStartTask();
    void onStopTask();
    void onResetTask();
    void onEstopTask();          // hard emergency stop - bypasses confirmation
    void onPollTaskStatus();     // periodic poll of task.get_status

    void onStartResult(QJsonObject result);
    void onStopResult(QJsonObject result);

private:
    void buildUi();
    QWidget* build3DPlaceholder();
    QWidget* buildTaskPanel();
    void updateStatusFromBackend(const QJsonObject &status);

    RpcClient   *rpc_;
    QListWidget *task_list_;
    QPushButton *btn_start_;
    QPushButton *btn_stop_;
    QPushButton *btn_estop_;     // big red emergency stop
    QPushButton *btn_reset_;
    QLabel      *task_status_label_;
    LogWidget   *log_widget_;
    class QTimer *poll_timer_;
};

#endif // TAB4TASKCONFIG_H
