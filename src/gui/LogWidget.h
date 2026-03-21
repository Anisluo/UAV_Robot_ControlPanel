#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QWidget>
#include <QString>

class QPlainTextEdit;
class QComboBox;
class QPushButton;

class LogWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogWidget(QWidget *parent = nullptr);

public slots:
    void appendLog(const QString &level, const QString &msg);
    // Overload for simple string messages (level embedded or INFO by default)
    void appendLogMsg(const QString &msg);

private slots:
    void clearLog();

private:
    QPlainTextEdit *log_edit_;
    QComboBox      *filter_combo_;
    QPushButton    *clear_btn_;

    QString colorForLevel(const QString &level) const;
    bool    levelPassesFilter(const QString &level) const;
};

#endif // LOGWIDGET_H
