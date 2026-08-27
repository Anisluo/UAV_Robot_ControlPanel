#ifndef TASKDISPATCHPANEL_H
#define TASKDISPATCHPANEL_H

#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QFrame;
class QProcess;

// 4-stage demo of the JK task pipeline:
//   1) 接收任务   2) 解析任务   3) 匹配选取航线   4) 下发任务
// Drives status text + content into 4 stacked cards on a single timer.
// Source library dir + target drone id come from the surrounding
// DroneWidget; they're injected via setKmzLibraryDir / setDroneId.
class TaskDispatchPanel : public QWidget {
    Q_OBJECT
public:
    explicit TaskDispatchPanel(QWidget *parent = nullptr);

    void setKmzLibraryDir(const QString &dir);
    void setDroneId(const QString &id);

signals:
    // Stage-4 success: matched KMZ file path + drone identifier.
    // DroneWidget can chain a real TCP deploy off this.
    void taskDispatched(const QString &kmz_path, const QString &drone_id);

private slots:
    void onStartClicked();
    void onClearClicked();
    void onStepTimer();
    // Real daemon (kmz_dispatch_daemon.exe) stdout pump.
    void onDaemonStdout();
    void onDaemonStderr();
    void onDaemonFinished(int exit_code);

private:
    struct StageCard {
        QFrame         *frame  = nullptr;
        QLabel         *title  = nullptr;
        QLabel         *status = nullptr;
        QPlainTextEdit *body   = nullptr;
    };
    struct DemoTask {
        QString task_id;
        QString name;
        QString start;
        QString end;
        QString section_id;
        QString section_name;
        QString kmz;
        QString owner;
        int     tower  = 0;
        int     poles  = 0;
        int     status = 0;
    };

    void buildUi();
    void resetCards();
    void showCard(int idx, const QStringList &lines, const QString &state);
    static QVector<DemoTask> demoTasks();
    // Mirrors the Python match_kmz fallback ladder. Returns absolute path
    // on hit (empty on miss). On hit, *level_out gets a human-readable
    // match level; *candidate_count_out gets the # of .kmz in dir.
    static QString matchKmz(const QString &dir, const QString &name,
                            QString *level_out, int *candidate_count_out);
    // Mirrors Python seed_demo_kmz(): if the library has zero .kmz files,
    // drop the 3 demo航线 placeholders into it so the匹配 step can succeed
    // out-of-the-box. Returns true if seeding ran (i.e. dir existed and
    // was empty); false if dir already had files or could not be created.
    static bool seedDemoKmz(const QString &dir);

    // 启动下一轮 (轮询模式)。如果配置了 daemon exe 路径 → spawn kmz daemon
    // 一次性 cycle (--once)。否则 fallback 回原来的硬编码 demo 数据.
    void startCycle();
    // Spawn the real kmz daemon child process for one polling cycle.
    // Returns false if daemon binary is missing — caller should fall
    // back to demo data via the timer-driven onStepTimer path.
    bool startDaemonCycle();
    // Parse one log line from the daemon and possibly transition a card.
    void handleDaemonLine(const QString &line);

    QVector<StageCard>  cards_;
    QPushButton        *btn_start_  = nullptr;
    QPushButton        *btn_clear_  = nullptr;
    QTimer             *step_timer_ = nullptr;

    QString kmz_dir_;
    QString drone_id_;

    DemoTask current_;
    int      stage_ = 0;     // 0 = idle/just-started, 1..4 = running that stage
    QString  matched_path_;
    bool     match_ok_ = false;

    // 轮询模式: ▶ 开始 一次后, 4 步走完会自动等 kCycleGapMs 再发起下一轮.
    // ⏸ 停止 (按钮在轮询时变这个文案) 可中止. clear 直接停掉.
    bool     polling_active_ = false;
    int      cycle_count_    = 0;
    QLabel  *cycle_label_    = nullptr;   // header 右上角 "轮询 #N · 等待 X 秒"

    // Real-daemon path: when the bundled kmz_dispatch_daemon.exe is
    // present, polls drive the real platform (SM4 + JWT + 30 s tick).
    // Otherwise fall back to the in-memory demoTasks() so the UI still
    // does something useful for the operator demo.
    QString  daemon_exe_path_;
    QString  daemon_watch_folder_;
    QProcess *daemon_proc_   = nullptr;
    QString  daemon_buf_;                 // partial line accumulator (stdout)
    bool     stage_marked_[4] = { false, false, false, false };  // per-cycle
    QHash<QString, QString> last_task_;   // parsed key→value pairs of current task
};

#endif // TASKDISPATCHPANEL_H
