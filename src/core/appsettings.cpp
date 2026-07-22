#include "core/appsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace lighttunnel {

QString coreTypeKey(CoreType type)
{
    return type == CoreType::Xray ? QStringLiteral("xray") : QStringLiteral("sing-box");
}

QString coreDisplayName(CoreType type)
{
    return type == CoreType::Xray ? QStringLiteral("Xray") : QStringLiteral("sing-box");
}

CoreType coreTypeFromKey(const QString &key)
{
    return key.compare(QStringLiteral("xray"), Qt::CaseInsensitive) == 0
        ? CoreType::Xray
        : CoreType::SingBox;
}

AppSettings AppSettings::load()
{
    QSettings settings;
    AppSettings value;
    value.coreType = coreTypeFromKey(settings.value(QStringLiteral("core/type"),
                                                     QStringLiteral("sing-box")).toString());
    const QString legacyPath = settings.value(QStringLiteral("core/path")).toString();
    value.singBoxPath = settings.value(
        QStringLiteral("core/singBoxPath"),
        legacyPath.contains(QStringLiteral("sing-box"), Qt::CaseInsensitive)
            ? legacyPath
            : discoverCore(CoreType::SingBox)).toString();
    value.xrayPath = settings.value(
        QStringLiteral("core/xrayPath"),
        legacyPath.contains(QStringLiteral("xray"), Qt::CaseInsensitive)
            ? legacyPath
            : discoverCore(CoreType::Xray)).toString();
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
    settings.setValue(QStringLiteral("core/type"), coreTypeKey(coreType));
    settings.setValue(QStringLiteral("core/singBoxPath"), singBoxPath);
    settings.setValue(QStringLiteral("core/xrayPath"), xrayPath);
    settings.remove(QStringLiteral("core/path"));
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

QString AppSettings::corePath() const
{
    return coreType == CoreType::Xray ? xrayPath : singBoxPath;
}

void AppSettings::setCorePath(const QString &path)
{
    if (coreType == CoreType::Xray) {
        xrayPath = path.trimmed();
    } else {
        singBoxPath = path.trimmed();
    }
}

QString AppSettings::discoverCore(CoreType type)
{
    const bool xray = type == CoreType::Xray;
    const QString binaryName = xray ? QStringLiteral("xray") : QStringLiteral("sing-box");
    const QDir managedDirectory(managedCoreDirectory(type));
    const QFileInfoList managedCores = managedDirectory.entryInfoList(
        {binaryName + QStringLiteral("-*")}, QDir::Files | QDir::Executable, QDir::Time);
    if (!managedCores.isEmpty()) {
        return managedCores.constFirst().absoluteFilePath();
    }

    const QString fromPath = QStandardPaths::findExecutable(binaryName);
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QStringList candidates = xray
        ? QStringList{
              QStringLiteral("/usr/local/bin/xray"),
              QStringLiteral("/usr/bin/xray"),
              QDir::home().filePath(QStringLiteral(".local/share/v2rayN/bin/xray/xray")),
          }
        : QStringList{
              QStringLiteral("/usr/local/bin/sing-box"),
              QStringLiteral("/usr/bin/sing-box"),
              QDir::home().filePath(QStringLiteral(".local/share/v2rayN/bin/sing_box/sing-box")),
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

QString AppSettings::managedCoreDirectory(CoreType type)
{
    return QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                    .filePath(QStringLiteral("cores")))
        .filePath(coreTypeKey(type));
}

} // namespace lighttunnel
