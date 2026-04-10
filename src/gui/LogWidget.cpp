#include "LogWidget.h"
#include "core/RpcClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QScrollBar>
#include <QTimer>
#include <QJsonObject>

namespace {
// Display name → backend `source` token sent to system.get_logs.
// Empty string means "local" (no remote fetch).
struct SourceEntry { const char *label; const char *token; };
constexpr SourceEntry kSources[] = {
    {"本地",          ""},
    {"uav_robotd",    "robotd"},
    {"proc_arm",      "proc_arm"},
    {"proc_gateway",  "proc_gateway"},
    {"proc_npu",      "proc_npu"},
    {"proc_realsense","proc_realsense"},
    {"proc_grasp",    "proc_grasp"},
};
}  // namespace

LogWidget::LogWidget(RpcClient *rpc, QWidget *parent)
    : QWidget(parent)
    , rpc_(rpc)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Toolbar
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);

    auto *sourceLabel = new QLabel("日志源:", this);
    sourceLabel->setFixedWidth(48);
    toolbar->addWidget(sourceLabel);

    source_combo_ = new QComboBox(this);
    for (const auto &e : kSources) {
        source_combo_->addItem(QString::fromUtf8(e.label),
                               QString::fromUtf8(e.token));
    }
    source_combo_->setFixedWidth(130);
    toolbar->addWidget(source_combo_);

    auto *levelLabel = new QLabel("级别:", this);
    levelLabel->setFixedWidth(34);
    toolbar->addWidget(levelLabel);

    filter_combo_ = new QComboBox(this);
    filter_combo_->addItems({"全部", "信息", "警告", "错误"});
    filter_combo_->setFixedWidth(80);
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

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(1500);
    connect(poll_timer_, &QTimer::timeout, this, &LogWidget::pollRemoteLogs);

    connect(clear_btn_, &QPushButton::clicked, this, &LogWidget::clearLog);
    connect(source_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogWidget::onSourceChanged);
}

void LogWidget::onSourceChanged(int index)
{
    if (index < 0 || index >= source_combo_->count()) return;
    const QString token = source_combo_->itemData(index).toString();
    in_remote_mode_     = !token.isEmpty();
    last_remote_logs_.clear();
    log_edit_->clear();
    if (in_remote_mode_) {
        if (rpc_ == nullptr) {
            log_edit_->appendPlainText("[日志] RPC client not available.");
            return;
        }
        log_edit_->appendPlainText(QString("[日志] 正在从 %1 拉取日志…")
                                       .arg(source_combo_->currentText()));
        pollRemoteLogs();          // immediate first fetch
        poll_timer_->start();
    } else {
        poll_timer_->stop();
    }
}

void LogWidget::pollRemoteLogs()
{
    if (!in_remote_mode_ || rpc_ == nullptr || !rpc_->isConnected()) return;
    const int idx = source_combo_->currentIndex();
    if (idx < 0) return;
    const QString token = source_combo_->itemData(idx).toString();
    if (token.isEmpty()) return;

    QJsonObject params;
    params["source"]    = token;
    params["max_lines"] = 200;

    rpc_->call("system.get_logs", params, [this, token](QJsonObject result) {
        // Ignore stale callbacks if user has switched away.
        if (!in_remote_mode_) return;
        const int curIdx = source_combo_->currentIndex();
        if (curIdx < 0) return;
        if (source_combo_->itemData(curIdx).toString() != token) return;
        const QString logs = result.value("logs").toString();
        renderRemoteLogs(logs);
    });
}

void LogWidget::renderRemoteLogs(const QString &text)
{
    if (text == last_remote_logs_) return;          // unchanged → no flicker
    last_remote_logs_ = text;

    QScrollBar *sb = log_edit_->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;

    log_edit_->clear();
    if (text.isEmpty()) {
        log_edit_->appendPlainText("[日志] 远端无日志输出。");
    } else {
        log_edit_->setPlainText(text);
    }
    if (atBottom) {
        sb->setValue(sb->maximum());
    }
}

void LogWidget::appendLog(const QString &level, const QString &msg)
{
    // In remote mode the view is owned by the polling fetcher; suppress
    // locally-generated lines so they don't interleave with remote logs.
    if (in_remote_mode_) return;
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
    last_remote_logs_.clear();
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
