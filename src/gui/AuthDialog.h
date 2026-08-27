#ifndef AUTHDIALOG_H
#define AUTHDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;

// AuthDialog — 管理员登录弹窗.
//
// Gates the ⚙ per-stage configuration buttons on the 任务配置 page. A running
// battery-swap cell is operated by people who are not supposed to be editing
// the task scripts, and the gear sits right on the flow chart they watch all
// day; one stray click used to drop them into a full script editor.
//
// This is an OPERATIONAL gate, not a security boundary. The credential is
// compiled in and the unlocked flag lives in GUI memory — anyone with the
// binary or a debugger walks straight past it. It exists to make "配置" a
// deliberate act, nothing more. Do not reuse it to protect anything that
// actually matters (nothing here is checked on the robot side, and the
// robot's own RPC has no notion of who is calling).
class AuthDialog : public QDialog {
    Q_OBJECT
public:
    explicit AuthDialog(QWidget *parent = nullptr);

    // Convenience: run the dialog modally, return true iff the operator
    // authenticated. Handles the retry loop internally.
    static bool authenticate(QWidget *parent);

private slots:
    void onSubmit();

private:
    QLineEdit *user_edit_ = nullptr;
    QLineEdit *pass_edit_ = nullptr;
    QLabel    *msg_label_ = nullptr;
    int        attempts_left_ = 3;
};

#endif // AUTHDIALOG_H
