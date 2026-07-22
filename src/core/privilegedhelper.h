#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace lighttunnel {

struct PrivilegedCommand final {
    QString program;
    QStringList arguments;
};

class PrivilegedHelper final {
public:
    static constexpr qsizetype MaxRequestSize = 64 * 1024;

    // Runs the root-side, line-delimited JSON protocol until the GUI closes
    // its pipe. This entry point must only be reached through pkexec.
    [[nodiscard]] static int run();

    // Public for focused security tests. The helper never accepts a unit name,
    // uid, gid or executable command from the unprivileged caller.
    [[nodiscard]] static std::optional<PrivilegedCommand> commandForRequest(
        const QJsonObject &request,
        quint32 callerUid,
        quint32 callerGid,
        QString *error = nullptr);
};

} // namespace lighttunnel
