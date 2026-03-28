#include <QApplication>
#include <QNetworkProxy>
#include <QIcon>
#include "gui/MainWindow.h"
#include "gui/AppIcon.h"
#include "gui/StyleSheet.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Disable system proxy: Qt picks up HTTP_PROXY env vars by default,
    // which causes QTcpSocket to send HTTP CONNECT tunnels instead of
    // direct TCP connections, breaking the RPC/video streams.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    app.setApplicationName("HostGUI");
    app.setApplicationDisplayName("无人机机器人控制台");
    app.setOrganizationName("RoboticsLab");
    app.setOrganizationDomain("roboticslab.local");
    app.setWindowIcon(createAppIcon());

    // Apply dark scientific theme globally
    app.setStyleSheet(StyleSheet::darkTheme());

    MainWindow window;
    window.setWindowIcon(createAppIcon());
    window.show();

    return app.exec();
}
