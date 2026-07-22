#include "core/singboxconfigbuilder.h"

#include "core/networkconstants.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace lighttunnel {
namespace {

QJsonArray stringArray(std::initializer_list<QString> values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

} // namespace

QJsonObject SingBoxConfigBuilder::build(const VlessProfile &profile,
                                         const AppSettings &settings,
                                         const QString &networkInterface)
{
    const QJsonObject log{
        {QStringLiteral("level"), QStringLiteral("info")},
        {QStringLiteral("timestamp"), true},
    };

    const QJsonObject remoteDns{
        {QStringLiteral("type"), QStringLiteral("https")},
        {QStringLiteral("tag"), QStringLiteral("remote_dns")},
        {QStringLiteral("server"), QStringLiteral("1.1.1.1")},
        {QStringLiteral("server_port"), 443},
        {QStringLiteral("path"), QStringLiteral("/dns-query")},
        {QStringLiteral("detour"), QStringLiteral("proxy")},
        {QStringLiteral("tls"), QJsonObject{
             {QStringLiteral("enabled"), true},
             {QStringLiteral("server_name"), QStringLiteral("cloudflare-dns.com")},
         }},
    };
    const QJsonObject localDns{
        {QStringLiteral("type"), QStringLiteral("local")},
        {QStringLiteral("tag"), QStringLiteral("local_dns")},
    };
    const QJsonObject dns{
        {QStringLiteral("servers"), QJsonArray{remoteDns, localDns}},
        {QStringLiteral("final"), QStringLiteral("remote_dns")},
        {QStringLiteral("strategy"), settings.forceIpv4
             ? QStringLiteral("ipv4_only") : QStringLiteral("prefer_ipv4")},
    };

    const QJsonObject mixedInbound{
        {QStringLiteral("type"), QStringLiteral("mixed")},
        {QStringLiteral("tag"), QStringLiteral("mixed-in")},
        {QStringLiteral("listen"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("listen_port"), LocalSocksPort},
    };
    const QJsonObject tunInbound{
        {QStringLiteral("type"), QStringLiteral("tun")},
        {QStringLiteral("tag"), QStringLiteral("tun-in")},
        {QStringLiteral("interface_name"), QStringLiteral("lighttunnel")},
        {QStringLiteral("address"), stringArray({QStringLiteral("172.19.0.1/30"),
                                                   QStringLiteral("fdfe:dcba:9876::1/126")})},
        {QStringLiteral("mtu"), settings.mtu},
        {QStringLiteral("auto_route"), true},
        {QStringLiteral("auto_redirect"), true},
        {QStringLiteral("strict_route"), true},
        {QStringLiteral("stack"), settings.tunStack},
    };

    QJsonObject directOutbound{
        {QStringLiteral("type"), QStringLiteral("direct")},
        {QStringLiteral("tag"), QStringLiteral("direct")},
    };
    if (!networkInterface.isEmpty()) {
        directOutbound.insert(QStringLiteral("bind_interface"), networkInterface);
    }

    QJsonArray routeRules{
        QJsonObject{
            {QStringLiteral("port"), 53},
            {QStringLiteral("action"), QStringLiteral("hijack-dns")},
        },
        QJsonObject{{QStringLiteral("action"), QStringLiteral("sniff")}},
        QJsonObject{
            {QStringLiteral("ip_is_private"), true},
            {QStringLiteral("outbound"), QStringLiteral("direct")},
        },
    };
    if (settings.forceIpv4) {
        routeRules.insert(1, QJsonObject{
            {QStringLiteral("ip_version"), 6},
            {QStringLiteral("action"), QStringLiteral("reject")},
        });
    }
    if (settings.blockQuic) {
        routeRules.append(QJsonObject{
            {QStringLiteral("network"), QStringLiteral("udp")},
            {QStringLiteral("port"), 443},
            {QStringLiteral("action"), QStringLiteral("reject")},
        });
    }

    const QJsonObject route{
        {QStringLiteral("auto_detect_interface"), true},
        {QStringLiteral("default_domain_resolver"), QJsonObject{
             {QStringLiteral("server"), QStringLiteral("local_dns")},
         }},
        {QStringLiteral("rules"), routeRules},
        {QStringLiteral("final"), QStringLiteral("proxy")},
    };

    return {
        {QStringLiteral("log"), log},
        {QStringLiteral("dns"), dns},
        {QStringLiteral("inbounds"), QJsonArray{mixedInbound, tunInbound}},
        {QStringLiteral("outbounds"), QJsonArray{
             buildProxyOutbound(profile, settings.forceIpv4, networkInterface),
             directOutbound,
         }},
        {QStringLiteral("route"), route},
    };
}

QJsonObject SingBoxConfigBuilder::buildProxyOutbound(const VlessProfile &profile,
                                                       bool forceIpv4,
                                                       const QString &networkInterface)
{
    QJsonObject outbound{
        {QStringLiteral("type"), QStringLiteral("vless")},
        {QStringLiteral("tag"), QStringLiteral("proxy")},
        {QStringLiteral("server"), profile.server},
        {QStringLiteral("server_port"), static_cast<int>(profile.serverPort)},
        {QStringLiteral("uuid"), profile.uuid},
        {QStringLiteral("packet_encoding"), QStringLiteral("xudp")},
        {QStringLiteral("domain_resolver"), QJsonObject{
             {QStringLiteral("server"), QStringLiteral("local_dns")},
             {QStringLiteral("strategy"), forceIpv4
                  ? QStringLiteral("ipv4_only") : QStringLiteral("prefer_ipv4")},
         }},
    };
    if (!profile.flow.isEmpty()) {
        outbound.insert(QStringLiteral("flow"), profile.flow);
    }
    if (!networkInterface.isEmpty()) {
        outbound.insert(QStringLiteral("bind_interface"), networkInterface);
    }

    if (profile.security == QStringLiteral("tls") || profile.security == QStringLiteral("reality")) {
        QJsonObject tls{
            {QStringLiteral("enabled"), true},
            {QStringLiteral("server_name"), profile.serverName.isEmpty() ? profile.server : profile.serverName},
            {QStringLiteral("insecure"), profile.allowInsecure},
            {QStringLiteral("utls"), QJsonObject{
                 {QStringLiteral("enabled"), true},
                 {QStringLiteral("fingerprint"), profile.fingerprint},
             }},
        };
        if (profile.security == QStringLiteral("reality")) {
            tls.insert(QStringLiteral("reality"), QJsonObject{
                {QStringLiteral("enabled"), true},
                {QStringLiteral("public_key"), profile.publicKey},
                {QStringLiteral("short_id"), profile.shortId},
            });
        }
        if (!profile.alpn.isEmpty()) {
            tls.insert(QStringLiteral("alpn"), QJsonArray::fromStringList(profile.alpn));
        }
        outbound.insert(QStringLiteral("tls"), tls);
    }

    if (profile.transport == QStringLiteral("ws")) {
        QJsonObject transport{
            {QStringLiteral("type"), QStringLiteral("ws")},
            {QStringLiteral("path"), profile.path.isEmpty() ? QStringLiteral("/") : profile.path},
        };
        if (!profile.host.isEmpty()) {
            transport.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Host"), profile.host}});
        }
        if (profile.maxEarlyData > 0) {
            transport.insert(QStringLiteral("max_early_data"), profile.maxEarlyData);
            transport.insert(QStringLiteral("early_data_header_name"),
                             profile.earlyDataHeader.isEmpty()
                                 ? QStringLiteral("Sec-WebSocket-Protocol")
                                 : profile.earlyDataHeader);
        }
        outbound.insert(QStringLiteral("transport"), transport);
    } else if (profile.transport == QStringLiteral("grpc")) {
        outbound.insert(QStringLiteral("transport"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("grpc")},
            {QStringLiteral("service_name"), profile.serviceName},
        });
    } else if (profile.transport == QStringLiteral("httpupgrade")) {
        QJsonObject transport{
            {QStringLiteral("type"), QStringLiteral("httpupgrade")},
            {QStringLiteral("path"), profile.path.isEmpty() ? QStringLiteral("/") : profile.path},
        };
        if (!profile.host.isEmpty()) {
            transport.insert(QStringLiteral("host"), profile.host);
        }
        outbound.insert(QStringLiteral("transport"), transport);
    }
    return outbound;
}

bool SingBoxConfigBuilder::writeSecurely(const QJsonObject &config,
                                          const QString &path,
                                          QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать runtime-каталог");
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось открыть конфигурацию: %1").arg(file.errorString());
        }
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray payload = QJsonDocument(config).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось записать конфигурацию: %1").arg(file.errorString());
        }
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

} // namespace lighttunnel
