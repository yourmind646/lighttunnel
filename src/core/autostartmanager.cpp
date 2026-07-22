#include "core/autostartmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace lighttunnel {

QString AutostartManager::desktopFilePath()
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configHome).filePath(QStringLiteral("autostart/io.github.lighttunnel.LightTunnel.desktop"));
}

bool AutostartManager::isEnabled()
{
    return QFile::exists(desktopFilePath());
}

bool AutostartManager::setEnabled(bool enabled, QString *error)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        if (!QFile::exists(path) || QFile::remove(path)) {
            return true;
        }
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось удалить файл автозапуска");
        }
        return false;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать каталог автозапуска");
        }
        return false;
    }

    const QString executable = QCoreApplication::applicationFilePath();
    const QString content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=LightTunnel\n"
        "Comment=Lightweight VLESS VPN client\n"
        "Exec=\"%1\"\n"
        "Icon=lighttunnel\n"
        "Terminal=false\n"
        "Categories=Network;Security;\n"
        "StartupNotify=false\n"
        "X-GNOME-Autostart-enabled=true\n")
                                .arg(executable);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(content.toUtf8()) < 0 || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось записать файл автозапуска: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace lighttunnel
