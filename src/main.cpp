#include "ui/mainwindow.h"

#include "core/privilegedhelper.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStyleFactory>

#include <unistd.h>

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
    const bool helperRequested = argc >= 2
        && QByteArray(argv[1]) == QByteArrayLiteral("--privileged-helper");
    if (helperRequested) {
        if (argc != 2) {
            return 64;
        }
        QCoreApplication helperApplication(argc, argv);
        return lighttunnel::PrivilegedHelper::run();
    }
    // The graphical application must never run as root, including through a
    // manually constructed pkexec invocation.
    if (::geteuid() == 0) {
        return 77;
    }

    QApplication application(argc, argv);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
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
