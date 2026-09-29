#include "MainWindow.h"
#include "ConnectionSettings.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("line-hmi-qt"));
    QApplication::setOrganizationName(QStringLiteral("SCARA"));

    const ConnectionSettings cfg = ConnectionSettings::load();
    ConnectionSettings::applyShowCursor(cfg.showCursor);

    MainWindow window;
#ifdef Q_OS_ANDROID
    window.setWindowState(Qt::WindowFullScreen);
    window.showFullScreen();
#else
    window.showFullScreen();
#endif
    return app.exec();
}
