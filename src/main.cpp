#include <QApplication>
#include <QNetworkProxy>
#include <QIcon>
#include <QSettings>
#include <QtGlobal>
#include <QDebug>
#include "gui/MainWindow.h"
#include "gui/AppIcon.h"
#include "gui/StyleSheet.h"

int main(int argc, char *argv[])
{
#if defined(Q_OS_LINUX)
    // CentOS/RHEL desktop sessions are commonly X11-based. When no explicit Qt
    // platform is configured and a DISPLAY is present, prefer xcb to avoid
    // platform plugin ambiguity between Wayland and X11.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

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

    // Persist UI parameters in a human-readable INI file. Using IniFormat
    // gives us a portable file under the user's config directory:
    //   Windows: %APPDATA%/RoboticsLab/HostGUI.ini
    //   Linux:   ~/.config/RoboticsLab/HostGUI.ini
    // All widgets simply construct `QSettings settings;` and the lookup
    // routes to this file automatically via the org/app name set above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    {
        QSettings probe;
        qInfo() << "[HostGUI] config file:" << probe.fileName();
    }

    // Apply dark scientific theme globally
    app.setStyleSheet(StyleSheet::darkTheme());

    MainWindow window;
    window.setWindowIcon(createAppIcon());
    window.show();

    return app.exec();
}
