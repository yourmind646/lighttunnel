#include "core/vlessprofile.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace lighttunnel {
namespace {

QString queryValue(const QUrlQuery &query, const QString &key)
{
    return query.queryItemValue(key, QUrl::FullyDecoded).trimmed();
}

bool parseBoolean(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("1") || normalized == QStringLiteral("true")
        || normalized == QStringLiteral("yes");
}

QString normalizedTransport(QString transport)
{
    transport = transport.trimmed().toLower();
    if (transport.isEmpty() || transport == QStringLiteral("raw")) {
        return QStringLiteral("tcp");
    }
    return transport;
}

} // namespace

std::optional<VlessProfile> VlessProfile::fromUri(const QString &uri, QString *error)
{
    const QUrl url(uri.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("vless"), Qt::CaseInsensitive) != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Ожидалась корректная ссылка vless://");
        }
        return std::nullopt;
    }

    VlessProfile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.uuid = url.userName(QUrl::FullyDecoded).trimmed();
    profile.server = url.host(QUrl::FullyDecoded).trimmed();
    const int parsedPort = url.port(443);
    if (parsedPort > 0 && parsedPort <= 65535) {
        profile.serverPort = static_cast<quint16>(parsedPort);
    }
    profile.name = url.fragment(QUrl::FullyDecoded).trimmed();

    const QUrlQuery query(url);
    profile.flow = queryValue(query, QStringLiteral("flow"));
    profile.security = queryValue(query, QStringLiteral("security")).toLower();
    if (profile.security.isEmpty()) {
        profile.security = QStringLiteral("none");
    }
    profile.serverName = queryValue(query, QStringLiteral("sni"));
    profile.fingerprint = queryValue(query, QStringLiteral("fp"));
    if (profile.fingerprint.isEmpty()) {
        profile.fingerprint = QStringLiteral("chrome");
    }
    profile.publicKey = queryValue(query, QStringLiteral("pbk"));
    profile.shortId = queryValue(query, QStringLiteral("sid"));
    profile.alpn = queryValue(query, QStringLiteral("alpn")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    profile.transport = normalizedTransport(queryValue(query, QStringLiteral("type")));
    profile.path = queryValue(query, QStringLiteral("path"));
    profile.host = queryValue(query, QStringLiteral("host"));
    profile.serviceName = queryValue(query, QStringLiteral("serviceName"));
    profile.maxEarlyData = queryValue(query, QStringLiteral("ed")).toInt();
    profile.earlyDataHeader = queryValue(query, QStringLiteral("eh"));
    profile.allowInsecure = parseBoolean(queryValue(query, QStringLiteral("allowInsecure")));

    if (profile.name.isEmpty()) {
        profile.name = profile.endpoint();
    }

    const QString validation = profile.validationError();
    if (!validation.isEmpty()) {
        if (error != nullptr) {
            *error = validation;
        }
        return std::nullopt;
    }
    return profile;
}

std::optional<VlessProfile> VlessProfile::fromJson(const QJsonObject &object, QString *error)
{
    VlessProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.name = object.value(QStringLiteral("name")).toString();
    profile.server = object.value(QStringLiteral("server")).toString();
    const int port = object.value(QStringLiteral("serverPort")).toInt(443);
    if (port > 0 && port <= 65535) {
        profile.serverPort = static_cast<quint16>(port);
    }
    profile.uuid = object.value(QStringLiteral("uuid")).toString();
    profile.flow = object.value(QStringLiteral("flow")).toString();
    profile.security = object.value(QStringLiteral("security")).toString(QStringLiteral("none"));
    profile.serverName = object.value(QStringLiteral("serverName")).toString();
    profile.fingerprint = object.value(QStringLiteral("fingerprint")).toString(QStringLiteral("chrome"));
    profile.publicKey = object.value(QStringLiteral("publicKey")).toString();
    profile.shortId = object.value(QStringLiteral("shortId")).toString();
    const QJsonArray alpnArray = object.value(QStringLiteral("alpn")).toArray();
    for (const QJsonValue &value : alpnArray) {
        if (value.isString()) {
            profile.alpn.append(value.toString());
        }
    }
    profile.transport = normalizedTransport(object.value(QStringLiteral("transport")).toString());
    profile.path = object.value(QStringLiteral("path")).toString();
    profile.host = object.value(QStringLiteral("host")).toString();
    profile.serviceName = object.value(QStringLiteral("serviceName")).toString();
    profile.maxEarlyData = qMax(0, object.value(QStringLiteral("maxEarlyData")).toInt(0));
    profile.earlyDataHeader = object.value(QStringLiteral("earlyDataHeader")).toString();
    profile.allowInsecure = object.value(QStringLiteral("allowInsecure")).toBool(false);

    if (profile.id.isEmpty()) {
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (profile.name.isEmpty()) {
        profile.name = profile.endpoint();
    }

    const QString validation = profile.validationError();
    if (!validation.isEmpty()) {
        if (error != nullptr) {
            *error = validation;
        }
        return std::nullopt;
    }
    return profile;
}

QJsonObject VlessProfile::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("server"), server},
        {QStringLiteral("serverPort"), static_cast<int>(serverPort)},
        {QStringLiteral("uuid"), uuid},
        {QStringLiteral("flow"), flow},
        {QStringLiteral("security"), security},
        {QStringLiteral("serverName"), serverName},
        {QStringLiteral("fingerprint"), fingerprint},
        {QStringLiteral("publicKey"), publicKey},
        {QStringLiteral("shortId"), shortId},
        {QStringLiteral("alpn"), QJsonArray::fromStringList(alpn)},
        {QStringLiteral("transport"), transport},
        {QStringLiteral("path"), path},
        {QStringLiteral("host"), host},
        {QStringLiteral("serviceName"), serviceName},
        {QStringLiteral("maxEarlyData"), maxEarlyData},
        {QStringLiteral("earlyDataHeader"), earlyDataHeader},
        {QStringLiteral("allowInsecure"), allowInsecure},
    };
}

QString VlessProfile::endpoint() const
{
    const QString formattedHost = server.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(server)
        : server;
    return QStringLiteral("%1:%2").arg(formattedHost).arg(serverPort);
}

QString VlessProfile::validationError() const
{
    if (server.trimmed().isEmpty()) {
        return QStringLiteral("В профиле отсутствует адрес сервера");
    }
    if (serverPort == 0) {
        return QStringLiteral("Некорректный порт VLESS-сервера");
    }
    const QUuid parsedUuid(uuid);
    if (parsedUuid.isNull()) {
        return QStringLiteral("Некорректный UUID VLESS-профиля");
    }

    static const QSet<QString> supportedTransports{
        QStringLiteral("tcp"),
        QStringLiteral("ws"),
        QStringLiteral("grpc"),
        QStringLiteral("httpupgrade"),
    };
    if (!supportedTransports.contains(normalizedTransport(transport))) {
        return QStringLiteral("Транспорт «%1» пока не поддерживается").arg(transport);
    }

    if (security == QStringLiteral("reality")) {
        if (serverName.isEmpty() || publicKey.isEmpty()) {
            return QStringLiteral("Для Reality необходимы SNI и public key");
        }
    } else if (security != QStringLiteral("tls") && security != QStringLiteral("none")) {
        return QStringLiteral("Неподдерживаемый режим безопасности «%1»").arg(security);
    }
    return {};
}

} // namespace lighttunnel
