#include <QApplication>
#include "gui/MainWindow.h"
#include "gui/StyleSheet.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("HostGUI");
    app.setApplicationDisplayName("无人机机器人控制台");
    app.setOrganizationName("RoboticsLab");
    app.setOrganizationDomain("roboticslab.local");

    // Apply dark scientific theme globally
    app.setStyleSheet(StyleSheet::darkTheme());

    MainWindow window;
    window.show();

    return app.exec();
}
