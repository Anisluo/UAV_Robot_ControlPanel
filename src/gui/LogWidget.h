#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QWidget>
#include <QString>

class QPlainTextEdit;
class QComboBox;
class QPushButton;
class QTimer;
class RpcClient;

class LogWidget : public QWidget {
    Q_OBJECT
public:
    // rpc may be null; remote sources will be disabled in that case.
    explicit LogWidget(RpcClient *rpc = nullptr, QWidget *parent = nullptr);

public slots:
    void appendLog(const QString &level, const QString &msg);
    // Overload for simple string messages (level embedded or INFO by default)
    void appendLogMsg(const QString &msg);

private slots:
    void clearLog();
    void onSourceChanged(int index);
    void pollRemoteLogs();

private:
    QPlainTextEdit *log_edit_;
    QComboBox      *filter_combo_;
    QComboBox      *source_combo_;
    QPushButton    *clear_btn_;
    QTimer         *poll_timer_ = nullptr;
    RpcClient      *rpc_        = nullptr;
    QString         last_remote_logs_;
    bool            in_remote_mode_ = false;

    QString colorForLevel(const QString &level) const;
    bool    levelPassesFilter(const QString &level) const;
    void    renderRemoteLogs(const QString &text);
};

#endif // LOGWIDGET_H
