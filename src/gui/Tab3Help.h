#ifndef TAB3HELP_H
#define TAB3HELP_H

#include <QWidget>

class QTextBrowser;

class Tab3Help : public QWidget {
    Q_OBJECT
public:
    explicit Tab3Help(QWidget *parent = nullptr);

private:
    QTextBrowser *browser_;
    static QString helpHtml();
};

#endif // TAB3HELP_H
