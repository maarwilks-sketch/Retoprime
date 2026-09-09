#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QString>
#include <QVersionNumber>
#ifndef RETOPRIME_NO_APP_MAIN
#include "app/DesktopWindow.h"
#endif

namespace retoprime {

QString applicationName()
{
    return QStringLiteral("RETOPRIME");
}

QVersionNumber applicationVersion()
{
    return {1, 0, 0};
}

} // namespace retoprime

#ifndef RETOPRIME_NO_APP_MAIN
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(retoprime::applicationName());
    QCoreApplication::setApplicationVersion(retoprime::applicationVersion().toString());

    retoprime::DesktopWindow window;
    window.show();
    const auto arguments = app.arguments();
    if (arguments.size() == 4 && arguments[1] == "--smoke-test") {
        window.smokeTest(arguments[2], arguments[3]);
    }

    return app.exec();
}
#endif
