#include "core/systemdcommandbuilder.h"

#include <QFileInfo>

namespace lighttunnel {

QStringList SystemdCommandBuilder::startArguments(const QString &unitName,
                                                  const QString &corePath,
                                                  const QString &configPath,
                                                  quint32 userId,
                                                  quint32 groupId)
{
    const QString capabilities =
        QStringLiteral("CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE");
    const QString workingDirectory = QFileInfo(configPath).absolutePath();

    return {
        QStringLiteral("systemd-run"),
        QStringLiteral("--unit=%1").arg(unitName),
        QStringLiteral("--collect"),
        QStringLiteral("--quiet"),
        QStringLiteral("--property=Type=simple"),
        QStringLiteral("--property=Restart=no"),
        QStringLiteral("--property=KillMode=mixed"),
        QStringLiteral("--property=User=%1").arg(userId),
        QStringLiteral("--property=Group=%1").arg(groupId),
        QStringLiteral("--property=UMask=0077"),
        QStringLiteral("--property=NoNewPrivileges=true"),
        QStringLiteral("--property=ProtectSystem=strict"),
        QStringLiteral("--property=ProtectHome=read-only"),
        QStringLiteral("--property=PrivateTmp=true"),
        QStringLiteral("--property=CapabilityBoundingSet=%1").arg(capabilities),
        QStringLiteral("--property=AmbientCapabilities=%1").arg(capabilities),
        QStringLiteral("--working-directory=%1").arg(workingDirectory),
        corePath,
        QStringLiteral("run"),
        QStringLiteral("-c"),
        configPath,
        QStringLiteral("--disable-color"),
    };
}

} // namespace lighttunnel
