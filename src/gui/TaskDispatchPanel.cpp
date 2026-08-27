#include "TaskDispatchPanel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kStepDelayMs = 700;
constexpr int kCycleGapMs  = 3000;   // 轮询模式: 一轮 4 步走完后, 等多久再发起下一轮

struct StateStyle {
    QString marker;
    QString text;
    QString color;
};

const StateStyle &styleFor(const QString &state)
{
    static const StateStyle ok   { "✓", "完成",   "#1e7e34" };
    static const StateStyle fail { "✘", "失败",   "#c0392b" };
    static const StateStyle run  { "●", "进行中", "#1565c0" };
    static const StateStyle wait { "○", "等待",   "#888aaa" };
    if (state == "OK")   return ok;
    if (state == "FAIL") return fail;
    if (state == "RUN")  return run;
    if (state == "WAIT") return wait;
    return wait;
}

} // namespace


TaskDispatchPanel::TaskDispatchPanel(QWidget *parent)
    : QWidget(parent)
    , step_timer_(new QTimer(this))
{
    step_timer_->setSingleShot(true);
    connect(step_timer_, &QTimer::timeout, this, &TaskDispatchPanel::onStepTimer);

    // 自动定位 kmz_dispatch_daemon.exe — 真实平台对接 (SM4 + JWT + 30 s 轮询).
    // 优先级: QSettings → 已知绝对路径 → 同 HostGUI.exe 目录下 → 找不到则
    // fallback 到 demoTasks() 走假数据.
    QSettings s;
    QString cfg = s.value("TaskDispatchPanel/daemon_exe").toString();
    QStringList candidates;
    if (!cfg.isEmpty()) candidates << cfg;
    candidates
        << QStringLiteral("F:/ROBOT_ARM/jk/uav-encrypt-util/dist/kmz_dispatch_daemon.exe")
        << QCoreApplication::applicationDirPath() + "/kmz_dispatch_daemon.exe"
        << QCoreApplication::applicationDirPath() + "/jk/kmz_dispatch_daemon.exe";
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p)) { daemon_exe_path_ = p; break; }
    }
    daemon_watch_folder_ = s.value("TaskDispatchPanel/watch_folder",
        QStringLiteral("F:/ROBOT_ARM/jk/uav-encrypt-util/demo_inbox/M30T-A01")).toString();

    buildUi();
    resetCards();
}

void TaskDispatchPanel::setKmzLibraryDir(const QString &dir) { kmz_dir_ = dir; }
void TaskDispatchPanel::setDroneId(const QString &id)        { drone_id_ = id; }

void TaskDispatchPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *headTitle = new QLabel(
        QStringLiteral("任务流水线  —  接收 → 解析 → 匹配航线 → 下发"), this);
    headTitle->setStyleSheet(
        "color:#00c8d7; font-family:Consolas; font-weight:700;");
    cycle_label_ = new QLabel(QStringLiteral("待命"), this);
    cycle_label_->setStyleSheet(
        "color:#888aaa; font-family:Consolas;");
    btn_start_ = new QPushButton(QStringLiteral("▶  开始轮询"), this);
    btn_start_->setFixedHeight(28);
    btn_start_->setFixedWidth(110);
    btn_clear_ = new QPushButton(QStringLiteral("清空"), this);
    btn_clear_->setFixedHeight(28);
    btn_clear_->setFixedWidth(64);
    header->addWidget(headTitle);
    header->addStretch();
    header->addWidget(cycle_label_);
    header->addSpacing(10);
    header->addWidget(btn_start_);
    header->addWidget(btn_clear_);
    root->addLayout(header);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);

    const char *titles[4] = {
        "1) 接收到任务",
        "2) 解析任务",
        "3) 选取航线 (KMZ 库匹配)",
        "4) 下发航线 (推送至无人机)",
    };

    cards_.resize(4);
    for (int i = 0; i < 4; ++i) {
        auto &c = cards_[i];
        c.frame = new QFrame(this);
        c.frame->setFrameShape(QFrame::StyledPanel);
        c.frame->setStyleSheet(
            "QFrame { background:#1b1f2e; border:1px solid #2c3247;"
            "         border-radius:4px; }");

        auto *box = new QVBoxLayout(c.frame);
        box->setContentsMargins(6, 4, 6, 6);
        box->setSpacing(3);

        auto *bar = new QHBoxLayout;
        c.title = new QLabel(QString::fromUtf8(titles[i]), c.frame);
        c.title->setStyleSheet("color:#aab6cc; font-family:Consolas; font-weight:600;");
        c.status = new QLabel(QStringLiteral("○ 待开始"), c.frame);
        c.status->setStyleSheet("color:#888aaa; font-family:微软雅黑; font-weight:bold;");
        bar->addWidget(c.title);
        bar->addStretch();
        bar->addWidget(c.status);
        box->addLayout(bar);

        c.body = new QPlainTextEdit(c.frame);
        c.body->setReadOnly(true);
        c.body->setFixedHeight(132);
        c.body->setStyleSheet(
            "QPlainTextEdit { background:#0f1320; color:#cfd6e4;"
            "                 border:1px solid #2c3247; border-radius:3px;"
            "                 font-family:Consolas; font-size:11px; }");
        c.body->setLineWrapMode(QPlainTextEdit::NoWrap);
        box->addWidget(c.body);

        grid->addWidget(c.frame, i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);

    connect(btn_start_, &QPushButton::clicked, this, &TaskDispatchPanel::onStartClicked);
    connect(btn_clear_, &QPushButton::clicked, this, &TaskDispatchPanel::onClearClicked);
}

void TaskDispatchPanel::resetCards()
{
    for (auto &c : cards_) {
        const auto &s = styleFor("WAIT");
        c.status->setText(s.marker + " " + s.text);
        c.status->setStyleSheet(
            QString("color:%1; font-family:微软雅黑; font-weight:bold;").arg(s.color));
        c.body->clear();
    }
    stage_ = 0;
    match_ok_ = false;
    matched_path_.clear();
}

void TaskDispatchPanel::showCard(int idx, const QStringList &lines, const QString &state)
{
    if (idx < 0 || idx >= cards_.size()) return;
    auto &c = cards_[idx];
    const auto &s = styleFor(state);
    c.status->setText(s.marker + " " + s.text);
    c.status->setStyleSheet(
        QString("color:%1; font-family:微软雅黑; font-weight:bold;").arg(s.color));
    c.body->setPlainText(lines.join('\n'));
}

void TaskDispatchPanel::onClearClicked()
{
    if (step_timer_->isActive()) step_timer_->stop();
    if (daemon_proc_) {
        disconnect(daemon_proc_, nullptr, this, nullptr);
        daemon_proc_->kill();
        daemon_proc_->waitForFinished(2000);
        daemon_proc_->deleteLater();
        daemon_proc_ = nullptr;
    }
    polling_active_ = false;
    cycle_count_ = 0;
    resetCards();
    if (btn_start_) btn_start_->setText(QStringLiteral("▶  开始轮询"));
    if (cycle_label_) {
        cycle_label_->setText(QStringLiteral("待命"));
        cycle_label_->setStyleSheet("color:#888aaa; font-family:Consolas;");
    }
}

void TaskDispatchPanel::onStartClicked()
{
    // 切换轮询. 第一次按 → 启动并跑第 1 轮; 再按 → 立刻停, 等当前 step 跑完
    // (避免半步状态), 但不再发起下一轮.
    if (polling_active_) {
        polling_active_ = false;
        if (btn_start_) btn_start_->setText(QStringLiteral("▶  开始轮询"));
        if (cycle_label_) {
            cycle_label_->setText(
                QStringLiteral("已停止 (共 %1 轮)").arg(cycle_count_));
            cycle_label_->setStyleSheet("color:#aab6cc; font-family:Consolas;");
        }
        // 不调 step_timer_->stop() — 让当前 stage 跑完, 但下一轮不会再排
        return;
    }
    polling_active_ = true;
    cycle_count_ = 0;
    if (btn_start_) btn_start_->setText(QStringLiteral("⏸  停止"));
    startCycle();
}

void TaskDispatchPanel::startCycle()
{
    if (!polling_active_) return;
    if (step_timer_->isActive()) step_timer_->stop();

    ++cycle_count_;
    resetCards();
    last_task_.clear();
    for (auto &m : stage_marked_) m = false;
    if (cycle_label_) {
        cycle_label_->setText(
            QStringLiteral("● 轮询中 · 第 %1 轮").arg(cycle_count_));
        cycle_label_->setStyleSheet("color:#1565c0; font-family:Consolas; font-weight:bold;");
    }

    // 真平台优先 — 调用 kmz_dispatch_daemon.exe --once. 启动失败再退回 demo.
    if (!daemon_exe_path_.isEmpty() && startDaemonCycle()) {
        return;
    }

    // Fallback: demoTasks 走假数据
    const auto pool = demoTasks();
    const int idx = QRandomGenerator::global()->bounded(pool.size());
    current_ = pool.at(idx);
    stage_ = 0;
    step_timer_->start(0);
}

bool TaskDispatchPanel::startDaemonCycle()
{
    if (daemon_proc_) {
        // 上一轮还没退? 杀掉再来
        disconnect(daemon_proc_, nullptr, this, nullptr);
        daemon_proc_->kill();
        daemon_proc_->waitForFinished(2000);
        daemon_proc_->deleteLater();
        daemon_proc_ = nullptr;
    }

    daemon_proc_ = new QProcess(this);
    daemon_proc_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(daemon_proc_, &QProcess::readyReadStandardOutput,
            this, &TaskDispatchPanel::onDaemonStdout);
    connect(daemon_proc_, &QProcess::readyReadStandardError,
            this, &TaskDispatchPanel::onDaemonStderr);
    connect(daemon_proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onDaemonFinished(code); });

    // 让 daemon 跑一次 (--once) 而不是常驻 30 s 循环; 我们在外面控轮询节奏.
    QStringList args;
    args << "--once";
    args << "--kmz-root"     << kmz_dir_;
    args << "--drone-id"     << (drone_id_.isEmpty()
                                 ? QStringLiteral("M30T-A01") : drone_id_);
    args << "--watch-folder" << daemon_watch_folder_;

    daemon_buf_.clear();
    showCard(0, {
        QStringLiteral("命令      : %1 --once")
            .arg(QFileInfo(daemon_exe_path_).fileName()),
        QStringLiteral("KMZ 根   : %1").arg(kmz_dir_),
        QStringLiteral("目标无人机: %1").arg(drone_id_),
        QStringLiteral("收件箱    : %1").arg(daemon_watch_folder_),
        QStringLiteral("状态      : 正在调用真平台 (SM4 + JWT)..."),
    }, "RUN");

    daemon_proc_->start(daemon_exe_path_, args);
    if (!daemon_proc_->waitForStarted(3000)) {
        showCard(0, {
            QStringLiteral("✗ daemon 启动失败:"),
            daemon_proc_->errorString(),
            QStringLiteral("回退到 demo 模式"),
        }, "FAIL");
        daemon_proc_->deleteLater();
        daemon_proc_ = nullptr;
        return false;
    }
    return true;
}

void TaskDispatchPanel::onDaemonStdout()
{
    if (!daemon_proc_) return;
    daemon_buf_ += QString::fromUtf8(daemon_proc_->readAllStandardOutput());
    int nl;
    while ((nl = daemon_buf_.indexOf('\n')) >= 0) {
        QString line = daemon_buf_.left(nl);
        daemon_buf_.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        handleDaemonLine(line);
    }
}

void TaskDispatchPanel::onDaemonStderr()
{
    if (!daemon_proc_) return;
    // Python 的 logging 模块默认写 stderr — 真实事件都在这里!
    const QString chunk = QString::fromUtf8(daemon_proc_->readAllStandardError());
    daemon_buf_ += chunk;
    int nl;
    while ((nl = daemon_buf_.indexOf('\n')) >= 0) {
        QString line = daemon_buf_.left(nl);
        daemon_buf_.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        handleDaemonLine(line);
    }
}

void TaskDispatchPanel::handleDaemonLine(const QString &line)
{
    // Lines look like:  17:25:56 [ INFO] message  or  17:25:56 [WARNING] ...
    // 我们关心几个关键词:
    //   "---- tick @"                → 已经在 startCycle 里标过 RUN, skip
    //   "login HTTP <code>"          → 写入 stage 1 (登录结果)
    //   "kmzName ="                  → 平台真的有任务回来! → stage 1 OK + stage 2 RUN
    //   "[dry-run] 平台桩响应"        → 同 kmzName, dry-run 路径
    //   "平台无新航线"                 → stage 1 FAIL (无任务), 整轮终止
    //   "COPY <src> -> <dst>"        → stage 3 (匹配命中) → stage 4 RUN
    //   "✔ ... 下发完成"              → stage 4 OK
    //   "✗" / "ERROR" / "FAIL"       → 对应卡片 FAIL

    static const QRegularExpression kKmzName(
        QStringLiteral("kmzName\\s*=\\s*(\\S+)"));
    static const QRegularExpression kCopy(
        QStringLiteral("COPY\\s+(.+?)\\s*->\\s*(.+)"));
    static const QRegularExpression kDispatched(
        QStringLiteral("([^\\s]+)\\s*->\\s*drone=([^\\s]+)\\s*下发完成"));
    static const QRegularExpression kLogin(
        QStringLiteral("login HTTP\\s*(\\d+)"));

    // ── 阶段 1: 接收任务 ──────────────────────────────────────────
    if (auto m = kKmzName.match(line); m.hasMatch()) {
        if (!stage_marked_[0]) {
            const QString kmz = m.captured(1);
            last_task_["kmzName"] = kmz;
            showCard(0, {
                QStringLiteral("时间      : %1")
                    .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")),
                QStringLiteral("kmzName   : %1").arg(kmz),
                QStringLiteral("来源      : 区域巡检平台 /api/task/getWaitDownloadList"),
                QStringLiteral("通道码    : wbjq  /  channelVersion 1.0.0.0"),
                QStringLiteral("状态      : ✔ 已收到 KMZ 名称"),
            }, "OK");
            stage_marked_[0] = true;
            // Stage 2 立即 RUN (基础解析就是从 kmzName 拆字段)
            showCard(1, {
                QStringLiteral("kmzName 来自平台: %1").arg(kmz),
                QStringLiteral("下一步      : 本机 KMZ 库匹配"),
            }, "OK");
            stage_marked_[1] = true;
        }
        return;
    }
    if (line.contains(QStringLiteral("平台无新航线"))) {
        showCard(0, {
            QStringLiteral("时间      : %1")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")),
            QStringLiteral("状态      : 平台无新航线 (或上游 502)"),
            QStringLiteral("下次轮询  : %1 秒后").arg(kCycleGapMs / 1000),
        }, "WAIT");
        stage_marked_[0] = true;          // 阶段 1 已经有结论 (无任务), 别再被
                                          // onDaemonFinished 覆盖成 "未触发".
        return;
    }
    if (auto m = kLogin.match(line); m.hasMatch()) {
        const int code = m.captured(1).toInt();
        if (code != 200) {
            QStringList lines = {
                QStringLiteral("时间      : %1")
                    .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")),
                QStringLiteral("登录响应  : HTTP %1").arg(code),
            };
            if (code == 502) lines << QStringLiteral("提示      : 上游网关返回 502, 平台暂不可达");
            showCard(0, lines, "WAIT");
            stage_marked_[0] = true;
        }
        return;
    }

    // ── 阶段 3: 本机库匹配/拷贝 ───────────────────────────────────
    if (auto m = kCopy.match(line); m.hasMatch()) {
        const QString src = m.captured(1);
        const QString dst = m.captured(2);
        showCard(2, {
            QStringLiteral("匹配命中  : %1").arg(QFileInfo(src).fileName()),
            QStringLiteral("源文件    : %1").arg(src),
            QStringLiteral("目标收件箱: %1").arg(QFileInfo(dst).path()),
            QStringLiteral("状态      : ✔ 文件已拷入收件箱"),
        }, "OK");
        stage_marked_[2] = true;
        return;
    }

    // ── 阶段 4: 下发完成 ───────────────────────────────────────────
    if (auto m = kDispatched.match(line); m.hasMatch()) {
        const QString kmz   = m.captured(1);
        const QString drone = m.captured(2);
        showCard(3, {
            QStringLiteral("目标无人机: %1").arg(drone),
            QStringLiteral("航线文件  : %1").arg(kmz),
            QStringLiteral("完成时间  : %1")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")),
            QStringLiteral("状态      : ✔ 下发完成"),
        }, "OK");
        stage_marked_[3] = true;
        emit taskDispatched(kmz, drone);
        return;
    }
    // 其他日志行 (login / tick / matching etc.) 我们暂时不显示在卡片上 —
    // 重要事件已经被上面的几个 regex 抓住了.
}

void TaskDispatchPanel::onDaemonFinished(int exit_code)
{
    if (daemon_proc_) {
        daemon_proc_->deleteLater();
        daemon_proc_ = nullptr;
    }
    // 收尾: 给那些没被日志触发的阶段填上更有意义的占位, 而不是统一
    // "未触发". 真实情况几乎总是 "因为上一阶段没结果, 这一步根本没必要跑".
    //
    // 阶段 1: 如果还没标记, 说明 daemon 退出但既没说"无任务"也没"有任务",
    // 大多是网络/启动异常. 标 "WAIT" + 提示.
    if (!stage_marked_[0]) {
        showCard(0, {
            QStringLiteral("时间      : %1")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")),
            QStringLiteral("状态      : 本轮未收到平台响应"),
            QStringLiteral("提示      : 检查 daemon / 网络 / 平台可达性"),
        }, "WAIT");
    }
    // 阶段 2 ~ 4: 只有阶段 1 有任务时才应该跑. 没跑就是预期内行为, 别让
    // 用户以为"出错跳过".
    auto skipBecauseNoTask = [this](int idx, const QString &reason) {
        if (!stage_marked_[idx]) {
            showCard(idx, {
                QStringLiteral("未执行原因 : %1").arg(reason),
                QStringLiteral("(只有阶段 1 收到任务后, 本步才会运行)"),
            }, "WAIT");
        }
    };
    skipBecauseNoTask(1, QStringLiteral("阶段 1 无任务可解析"));
    skipBecauseNoTask(2, QStringLiteral("阶段 2 无任务名可匹配"));
    skipBecauseNoTask(3, QStringLiteral("阶段 3 没有 KMZ 文件可下发"));

    if (polling_active_) {
        const QString tag = (exit_code == 0)
            ? QStringLiteral("✓ 第 %1 轮完成").arg(cycle_count_)
            : QStringLiteral("✗ daemon 退出码 %1").arg(exit_code);
        if (cycle_label_) cycle_label_->setText(
            QStringLiteral("%1 · %2 秒后轮询下一任务")
                .arg(tag).arg(kCycleGapMs / 1000));
        QTimer::singleShot(kCycleGapMs, this, [this]() {
            if (polling_active_) startCycle();
        });
    }
}

void TaskDispatchPanel::onStepTimer()
{
    ++stage_;

    switch (stage_) {
    case 1: {
        const QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        showCard(0, {
            QStringLiteral("时间        : %1").arg(now),
            QStringLiteral("任务编号    : %1").arg(current_.task_id),
            QStringLiteral("任务名称    : %1").arg(current_.name),
            QStringLiteral("时间窗      : %1  ~  %2").arg(current_.start, current_.end),
            QStringLiteral("杆塔总数    : %1").arg(current_.tower),
            QStringLiteral("来源        : 区域巡检平台 (轮询 A2)"),
            QStringLiteral("状态        : 待客户端处理"),
        }, "OK");
        step_timer_->start(kStepDelayMs);
        return;
    }
    case 2: {
        showCard(1, {
            QStringLiteral("区段编号    : %1").arg(current_.section_id),
            QStringLiteral("区段名称    : %1").arg(current_.section_name),
            QStringLiteral("杆塔数      : %1").arg(current_.poles),
            QStringLiteral("状态码      : %1  (20=已领取)").arg(current_.status),
            QStringLiteral("领取人      : %1").arg(current_.owner),
            QStringLiteral("关联航线名  : %1").arg(current_.kmz),
            QStringLiteral("下一步      : 在本机 KMZ 库中查找该航线"),
        }, "OK");
        step_timer_->start(kStepDelayMs);
        return;
    }
    case 3: {
        // If the library is empty (first run), drop in 3 demo placeholders
        // so the matching step can succeed without manual setup.
        const bool seeded = seedDemoKmz(kmz_dir_);

        QString level;
        int     n_candidates = 0;
        matched_path_ = matchKmz(kmz_dir_, current_.kmz, &level, &n_candidates);
        match_ok_ = !matched_path_.isEmpty();

        QStringList lines = {
            QStringLiteral("目标航线名  : %1").arg(current_.kmz),
            QStringLiteral("KMZ 库目录  : %1").arg(kmz_dir_.isEmpty()
                ? QStringLiteral("(未配置)") : kmz_dir_),
            QStringLiteral("库内候选数  : %1%2")
                .arg(n_candidates)
                .arg(seeded ? QStringLiteral("  (已自动播种演示航线)") : QString()),
        };
        if (match_ok_) {
            QFileInfo fi(matched_path_);
            lines << QStringLiteral("命中文件    : %1").arg(matched_path_);
            lines << QStringLiteral("匹配级别    : %1").arg(level);
            lines << QStringLiteral("文件大小    : %1 字节").arg(fi.size());
            showCard(2, lines, "OK");
            step_timer_->start(kStepDelayMs);
        } else {
            lines << QStringLiteral("匹配结果    : ✘ 未找到 (请补充该 KMZ 到库目录)");
            showCard(2, lines, "FAIL");
            showCard(3, { QStringLiteral("跳过：上一阶段未找到航线") }, "FAIL");
            // 跳过 stage 4 (没文件可下发), 让下一个 timer tick 直接进 stage 5
            // (轮询尾巴), 由那里发起下一轮.
            stage_ = 4;
            if (polling_active_) {
                if (cycle_label_) cycle_label_->setText(
                    QStringLiteral("↻ 第 %1 轮匹配失败 · %2 秒后再次轮询")
                        .arg(cycle_count_).arg(kCycleGapMs / 1000));
                step_timer_->start(kCycleGapMs);
            }
        }
        return;
    }
    case 4: {
        const QString drone = drone_id_.isEmpty()
            ? QStringLiteral("M30T-A01") : drone_id_;
        QFileInfo fi(matched_path_);
        showCard(3, {
            QStringLiteral("目标无人机  : %1").arg(drone),
            QStringLiteral("源文件      : %1").arg(matched_path_),
            QStringLiteral("文件名      : %1").arg(fi.fileName()),
            QStringLiteral("字节数      : %1").arg(fi.size()),
            QStringLiteral("完成时间    : %1")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")),
            QStringLiteral("结果        : ✔ 已交付下发模块"),
        }, "OK");
        emit taskDispatched(matched_path_, drone);
        // 轮询模式: 成功下发后, 等 kCycleGapMs 再发起下一轮
        if (polling_active_) {
            if (cycle_label_) cycle_label_->setText(
                QStringLiteral("✓ 第 %1 轮完成 · %2 秒后轮询下一任务")
                    .arg(cycle_count_).arg(kCycleGapMs / 1000));
            step_timer_->start(kCycleGapMs);
        }
        return;
    }
    case 5: {
        // 轮询尾巴: 由 stage 3 fail 或 stage 4 success 之后排过来, 触发新一轮
        if (polling_active_) {
            startCycle();
        }
        return;
    }
    default:
        return;
    }
}

QVector<TaskDispatchPanel::DemoTask> TaskDispatchPanel::demoTasks()
{
    QVector<DemoTask> out;
    out.reserve(3);

    DemoTask t1;
    t1.task_id      = "a9ee20d085c1f5006ee5b5ac89fecc52";
    t1.name         = QStringLiteral("FZ2026051500001 区域巡检测试");
    t1.start        = "2026-05-18";
    t1.end          = "2026-05-27";
    t1.tower        = 7;
    t1.section_id   = "02a68a843eb3a9031626e789b0b0d97b";
    t1.section_name = QStringLiteral("区域_1-1-区域_1-2");
    t1.kmz          = QStringLiteral("FZ2024060600005_区域_1.kmz");
    t1.poles        = 4;
    t1.status       = 20;
    t1.owner        = "fz_jq";
    out.append(t1);

    DemoTask t2;
    t2.task_id      = "b1cf330a8c7b4b138c2eb9a3a6d10ef9";
    t2.name         = QStringLiteral("FZ2026051500002 隧道沿线巡检");
    t2.start        = "2026-05-20";
    t2.end          = "2026-05-25";
    t2.tower        = 11;
    t2.section_id   = "ce14ab8c2e7d4e62a1b7a0f74ddee011";
    t2.section_name = QStringLiteral("隧道_A-起点-隧道_A-终点");
    t2.kmz          = QStringLiteral("FZ2025_隧道_A_line.kmz");
    t2.poles        = 6;
    t2.status       = 20;
    t2.owner        = "fz_jq";
    out.append(t2);

    DemoTask t3;
    t3.task_id      = "c5cd6e8e7a234ac1b3c84d8db2c7f111";
    t3.name         = QStringLiteral("FZ2026051500003 区域_2 复查");
    t3.start        = "2026-05-21";
    t3.end          = "2026-05-26";
    t3.tower        = 5;
    t3.section_id   = "f08e9c7d6a9b4128b9c7a6e210d23df0";
    t3.section_name = QStringLiteral("区域_2-1-区域_2-3");
    t3.kmz          = QStringLiteral("FZ2024060600005_区域_2.kmz");
    t3.poles        = 3;
    t3.status       = 20;
    t3.owner        = "fz_jq";
    out.append(t3);

    return out;
}

QString TaskDispatchPanel::matchKmz(const QString &dir, const QString &name,
                                     QString *level_out, int *candidate_count_out)
{
    if (level_out)            level_out->clear();
    if (candidate_count_out)  *candidate_count_out = 0;
    if (dir.isEmpty() || name.isEmpty()) return {};

    QDir d(dir);
    if (!d.exists()) return {};

    const QStringList files = d.entryList({"*.kmz"},
                                          QDir::Files | QDir::Readable,
                                          QDir::Name);
    if (candidate_count_out) *candidate_count_out = files.size();
    if (files.isEmpty()) return {};

    // 1) exact
    if (files.contains(name)) {
        if (level_out) *level_out = QStringLiteral("1) 精确匹配 (文件名完全相等)");
        return d.absoluteFilePath(name);
    }
    // 2) case-insensitive
    const QString name_lo = name.toLower();
    for (const QString &f : files) {
        if (f.toLower() == name_lo) {
            if (level_out) *level_out = QStringLiteral("2) 大小写不敏感匹配");
            return d.absoluteFilePath(f);
        }
    }
    // 3) base (strip extension), lowercase
    auto stripExt = [](const QString &s) {
        const int i = s.lastIndexOf('.');
        return (i < 0) ? s : s.left(i);
    };
    const QString base_lo = stripExt(name).toLower();
    for (const QString &f : files) {
        if (stripExt(f).toLower() == base_lo) {
            if (level_out) *level_out = QStringLiteral("3) 去扩展名后相等");
            return d.absoluteFilePath(f);
        }
    }
    // 4) substring containment
    for (const QString &f : files) {
        const QString f_lo = f.toLower();
        if (f_lo.contains(name_lo) || name_lo.contains(f_lo) || f_lo.contains(base_lo)) {
            if (level_out) *level_out = QStringLiteral("4) 子串包含");
            return d.absoluteFilePath(f);
        }
    }
    return {};
}

bool TaskDispatchPanel::seedDemoKmz(const QString &dir)
{
    if (dir.isEmpty()) return false;
    QDir d(dir);
    if (!d.exists() && !d.mkpath(".")) return false;

    if (!d.entryList({"*.kmz"}, QDir::Files | QDir::Readable).isEmpty()) {
        return false;
    }

    // Minimal KMZ-flavored payload — not a valid zip, but the matching
    // step only inspects file names, and downstream stage 4 just copies
    // bytes through. Real航线 files get dropped in by ops.
    static const QByteArray kPayload =
        QByteArrayLiteral("PK\x03\x04" "demo-kmz-placeholder\n");

    const QStringList names = {
        QStringLiteral("FZ2024060600005_区域_1.kmz"),
        QStringLiteral("FZ2024060600005_区域_2.kmz"),
        QStringLiteral("FZ2025_隧道_A_line.kmz"),
    };

    bool any_written = false;
    for (const QString &n : names) {
        QFile f(d.absoluteFilePath(n));
        if (!f.open(QIODevice::WriteOnly)) continue;
        f.write(kPayload);
        f.close();
        any_written = true;
    }
    return any_written;
}
