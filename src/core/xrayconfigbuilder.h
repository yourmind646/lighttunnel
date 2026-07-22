#pragma once

#include "core/appsettings.h"
#include "core/vlessprofile.h"

#include <QJsonObject>

namespace lighttunnel {

class XrayConfigBuilder final {
public:
    [[nodiscard]] static QJsonObject build(const VlessProfile &profile,
                                           const AppSettings &settings,
                                           const QString &networkInterface);
    [[nodiscard]] static bool writeSecurely(const QJsonObject &config,
                                            const QString &path,
                                            QString *error = nullptr);

private:
    [[nodiscard]] static QJsonObject buildProxyOutbound(const VlessProfile &profile,
                                                        bool forceIpv4,
                                                        const QString &networkInterface);
};

} // namespace lighttunnel
