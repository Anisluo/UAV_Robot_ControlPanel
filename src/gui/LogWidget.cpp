#include "LogWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QScrollBar>

LogWidget::LogWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Toolbar
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);

    auto *levelLabel = new QLabel("日志级别:", this);
    levelLabel->setFixedWidth(38);
    toolbar->addWidget(levelLabel);

    filter_combo_ = new QComboBox(this);
    filter_combo_->addItems({"全部", "信息", "警告", "错误"});
    filter_combo_->setFixedWidth(90);
    toolbar->addWidget(filter_combo_);

    toolbar->addStretch();

    clear_btn_ = new QPushButton("清除", this);
    clear_btn_->setFixedWidth(60);
    toolbar->addWidget(clear_btn_);

    layout->addLayout(toolbar);

    log_edit_ = new QPlainTextEdit(this);
    log_edit_->setReadOnly(true);
    log_edit_->setMaximumBlockCount(2000);
    layout->addWidget(log_edit_);

    connect(clear_btn_, &QPushButton::clicked, this, &LogWidget::clearLog);
}

void LogWidget::appendLog(const QString &level, const QString &msg)
{
    if (!levelPassesFilter(level)) return;

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString color     = colorForLevel(level);

    QString levelUpper = level.toUpper();
    QString html = QString("<span style='color:#555770;font-family:Consolas;'>%1</span> "
                           "<span style='color:%2;font-weight:bold;font-family:Consolas;'>[%3]</span> "
                           "<span style='color:%4;font-family:Consolas;'>%5</span>")
                   .arg(timestamp)
                   .arg(color)
                   .arg(levelUpper)
                   .arg(color)
                   .arg(msg.toHtmlEscaped());

    log_edit_->appendHtml(html);

    // Auto-scroll
    QScrollBar *sb = log_edit_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void LogWidget::appendLogMsg(const QString &msg)
{
    // Parse level from message prefix if present
    QString level = "INFO";
    QString text  = msg;

    if (msg.startsWith("[RPC]") || msg.startsWith("[Video]")) {
        level = "INFO";
    }
    if (msg.contains("error", Qt::CaseInsensitive) ||
        msg.contains("Error", Qt::CaseInsensitive)) {
        level = "ERROR";
    }
    if (msg.contains("warn", Qt::CaseInsensitive)) {
        level = "WARN";
    }

    appendLog(level, text);
}

void LogWidget::clearLog()
{
    log_edit_->clear();
}

QString LogWidget::colorForLevel(const QString &level) const
{
    QString lu = level.toUpper();
    if (lu == "ERROR") return "#f44336";
    if (lu == "WARN")  return "#ff9800";
    if (lu == "DEBUG") return "#888aaa";
    return "#dde1f0"; // INFO = white-ish
}

bool LogWidget::levelPassesFilter(const QString &level) const
{
    QString filter = filter_combo_->currentText();
    if (filter == "全部") return true;

    QString lu = level.toUpper();
    if (filter == "信息")  return lu == "INFO";
    if (filter == "警告")  return lu == "WARN";
    if (filter == "错误") return lu == "ERROR";
    return true;
}
