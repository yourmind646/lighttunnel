#include "core/appsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace lighttunnel {

AppSettings AppSettings::load()
{
    QSettings settings;
    AppSettings value;
    value.corePath = settings.value(QStringLiteral("core/path"), discoverCore()).toString();
    value.networkInterface = settings.value(QStringLiteral("network/interface")).toString();
    value.tunStack = settings.value(QStringLiteral("tun/stack"), QStringLiteral("system")).toString();
    value.mtu = settings.value(QStringLiteral("tun/mtu"), 1500).toInt();
    value.blockQuic = settings.value(QStringLiteral("network/blockQuic"), false).toBool();
    value.startMinimized = settings.value(QStringLiteral("ui/startMinimized"), false).toBool();
    value.autoConnect = settings.value(QStringLiteral("connection/autoConnect"), false).toBool();
    value.autostart = settings.value(QStringLiteral("ui/autostart"), false).toBool();
    value.autoUpdateCore = settings.value(QStringLiteral("core/autoUpdate"), true).toBool();
    value.lastProfileId = settings.value(QStringLiteral("profiles/lastId")).toString();
    return value;
}

void AppSettings::save() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("core/path"), corePath);
    settings.setValue(QStringLiteral("network/interface"), networkInterface);
    settings.setValue(QStringLiteral("tun/stack"), tunStack);
    settings.setValue(QStringLiteral("tun/mtu"), mtu);
    settings.setValue(QStringLiteral("network/blockQuic"), blockQuic);
    settings.setValue(QStringLiteral("ui/startMinimized"), startMinimized);
    settings.setValue(QStringLiteral("connection/autoConnect"), autoConnect);
    settings.setValue(QStringLiteral("ui/autostart"), autostart);
    settings.setValue(QStringLiteral("core/autoUpdate"), autoUpdateCore);
    settings.setValue(QStringLiteral("profiles/lastId"), lastProfileId);
    settings.sync();
}

QString AppSettings::discoverCore()
{
    const QDir managedDirectory(managedCoreDirectory());
    const QFileInfoList managedCores = managedDirectory.entryInfoList(
        {QStringLiteral("sing-box-*")}, QDir::Files | QDir::Executable, QDir::Time);
    if (!managedCores.isEmpty()) {
        return managedCores.constFirst().absoluteFilePath();
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("sing-box"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QStringList candidates{
        QDir::home().filePath(QStringLiteral(".local/share/v2rayN/bin/sing_box/sing-box")),
        QStringLiteral("/usr/local/bin/sing-box"),
        QStringLiteral("/usr/bin/sing-box"),
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

QString AppSettings::discoverDefaultInterface()
{
    QProcess process;
    process.start(QStringLiteral("ip"), {QStringLiteral("route"), QStringLiteral("show"), QStringLiteral("default")});
    if (!process.waitForFinished(2000) || process.exitCode() != 0) {
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList parts = output.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const qsizetype devIndex = parts.indexOf(QStringLiteral("dev"));
    if (devIndex >= 0 && devIndex + 1 < parts.size()) {
        return parts.at(devIndex + 1);
    }
    return {};
}

QString AppSettings::effectiveInterface() const
{
    return networkInterface.trimmed().isEmpty() ? discoverDefaultInterface() : networkInterface.trimmed();
}

QString AppSettings::runtimeDirectory()
{
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("runtime"));
    QDir().mkpath(path);
    return path;
}

QString AppSettings::managedCoreDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("core"));
}

} // namespace lighttunnel
