#pragma once

#include <QString>
#include <QStringList>

namespace lighttunnel {

class SystemdCommandBuilder final {
public:
    [[nodiscard]] static QStringList startArguments(const QString &unitName,
                                                    const QString &corePath,
                                                    const QString &configPath,
                                                    quint32 userId,
                                                    quint32 groupId);
};

} // namespace lighttunnel
