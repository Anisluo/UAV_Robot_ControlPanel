#include "AuthDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace {

// Compiled-in credential. See the class comment in AuthDialog.h — this is a
// misclick guard, not authentication. Kept as plain literals deliberately:
// hashing them would imply a secrecy this cannot provide, since the check
// happens entirely inside the GUI process.
const char kUser[] = "admin";
const char kPass[] = "admin";

} // namespace

AuthDialog::AuthDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("管理员登录"));
    setModal(true);
    // Fixed, compact — this is a credential prompt, not a panel.
    setMinimumWidth(300);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(10);

    auto *head = new QLabel(QStringLiteral("🔒  解锁任务步骤配置"), this);
    head->setStyleSheet("font-size:14px; font-weight:bold; color:#00c8d7;");
    root->addWidget(head);

    auto *hint = new QLabel(
        QStringLiteral("配置按钮 ⚙ 默认锁定，登录后本次运行内保持解锁。"), this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8a93a8; font-size:12px;");
    root->addWidget(hint);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(8);

    user_edit_ = new QLineEdit(this);
    user_edit_->setPlaceholderText(QStringLiteral("用户名"));
    pass_edit_ = new QLineEdit(this);
    pass_edit_->setPlaceholderText(QStringLiteral("密码"));
    pass_edit_->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("用户名:"), user_edit_);
    form->addRow(QStringLiteral("密  码:"), pass_edit_);
    root->addLayout(form);

    msg_label_ = new QLabel(QString(), this);
    msg_label_->setStyleSheet("color:#ff8a80; font-size:12px;");
    msg_label_->setWordWrap(true);
    root->addWidget(msg_label_);

    auto *bar = new QHBoxLayout();
    bar->addStretch(1);
    auto *btn_cancel = new QPushButton(QStringLiteral("取消"), this);
    auto *btn_ok     = new QPushButton(QStringLiteral("登录"), this);
    btn_ok->setDefault(true);
    bar->addWidget(btn_cancel);
    bar->addWidget(btn_ok);
    root->addLayout(bar);

    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_ok,     &QPushButton::clicked, this, &AuthDialog::onSubmit);
    // Enter in either field submits — the operator should never have to
    // reach for the mouse to get past a two-field prompt.
    connect(user_edit_, &QLineEdit::returnPressed, this, &AuthDialog::onSubmit);
    connect(pass_edit_, &QLineEdit::returnPressed, this, &AuthDialog::onSubmit);

    user_edit_->setFocus();
}

void AuthDialog::onSubmit()
{
    const QString u = user_edit_->text().trimmed();
    const QString p = pass_edit_->text();

    if (u == QLatin1String(kUser) && p == QLatin1String(kPass)) {
        accept();
        return;
    }

    // Wrong. Don't say WHICH field was wrong — and don't clear the username,
    // so a typo'd password is one keystroke from a retry.
    --attempts_left_;
    pass_edit_->clear();
    pass_edit_->setFocus();
    if (attempts_left_ <= 0) {
        msg_label_->setText(QStringLiteral("错误次数过多，已取消。"));
        reject();
        return;
    }
    msg_label_->setText(
        QStringLiteral("用户名或密码错误，还可尝试 %1 次。").arg(attempts_left_));
}

bool AuthDialog::authenticate(QWidget *parent)
{
    AuthDialog dlg(parent);
    return dlg.exec() == QDialog::Accepted;
}
