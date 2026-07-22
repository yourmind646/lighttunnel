#include "ui/mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

namespace {

QString loadStyleSheet()
{
    QFile file(QStringLiteral(":/styles/app.qss"));
    return file.open(QIODevice::ReadOnly | QIODevice::Text)
        ? QString::fromUtf8(file.readAll())
        : QString();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LightTunnel"));
    QApplication::setOrganizationDomain(QStringLiteral("io.github.lighttunnel"));
    QApplication::setApplicationName(QStringLiteral("LightTunnel"));
    QApplication::setApplicationDisplayName(QStringLiteral("LightTunnel"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.lighttunnel.LightTunnel"));
    QApplication::setQuitOnLastWindowClosed(false);
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/lighttunnel.svg")));
    application.setStyleSheet(loadStyleSheet());

    const QString runtimePath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QDir().mkpath(runtimePath);
    QLockFile instanceLock(QDir(runtimePath).filePath(QStringLiteral("lighttunnel.lock")));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) {
        QMessageBox::information(nullptr,
                                 QStringLiteral("LightTunnel"),
                                 QStringLiteral("LightTunnel уже запущен. Проверьте системный трей."));
        return 0;
    }

    lighttunnel::MainWindow window;
    window.handleStartup();
    return application.exec();
}
