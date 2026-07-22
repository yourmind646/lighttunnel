#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace lighttunnel {

struct VlessProfile final {
    QString id;
    QString name;
    QString server;
    quint16 serverPort{443};
    QString uuid;
    QString flow;
    QString security{QStringLiteral("none")};
    QString serverName;
    QString fingerprint{QStringLiteral("chrome")};
    QString publicKey;
    QString shortId;
    QStringList alpn;
    QString transport{QStringLiteral("tcp")};
    QString path;
    QString host;
    QString serviceName;
    QString xhttpMode;
    int maxEarlyData{0};
    QString earlyDataHeader;
    bool allowInsecure{false};

    [[nodiscard]] static std::optional<VlessProfile> fromUri(const QString &uri,
                                                              QString *error = nullptr);
    [[nodiscard]] static std::optional<VlessProfile> fromJson(const QJsonObject &object,
                                                               QString *error = nullptr);
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString validationError() const;
};

} // namespace lighttunnel

Q_DECLARE_METATYPE(lighttunnel::VlessProfile)
