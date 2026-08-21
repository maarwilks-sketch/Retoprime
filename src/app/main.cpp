#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QString>
#include <QVersionNumber>

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

    QMainWindow window;
    window.setWindowTitle(retoprime::applicationName());
    window.resize(1280, 800);
    window.show();

    return app.exec();
}
#endif
