#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("line-hmi-qt"));
    QApplication::setOrganizationName(QStringLiteral("SCARA"));

    MainWindow window;
    window.show();
    return app.exec();
}
