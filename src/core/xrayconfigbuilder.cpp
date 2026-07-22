#include "core/xrayconfigbuilder.h"

#include "core/networkconstants.h"
#include "core/singboxconfigbuilder.h"

#include <QJsonArray>

namespace lighttunnel {
namespace {

QJsonArray strings(std::initializer_list<QString> values)
{
    QJsonArray result;
    for (const QString &value : values) {
        result.append(value);
    }
    return result;
}

QString transportMethod(const QString &transport)
{
    if (transport == QStringLiteral("tcp")) {
        return QStringLiteral("raw");
    }
    if (transport == QStringLiteral("ws")) {
        return QStringLiteral("websocket");
    }
    return transport;
}

} // namespace

QJsonObject XrayConfigBuilder::build(const VlessProfile &profile,
                                     const AppSettings &settings,
                                     const QString &networkInterface)
{
    const QJsonObject tunInbound{
        {QStringLiteral("tag"), QStringLiteral("tun-in")},
        {QStringLiteral("protocol"), QStringLiteral("tun")},
        {QStringLiteral("settings"), QJsonObject{
             {QStringLiteral("name"), QStringLiteral("lighttunnel")},
             {QStringLiteral("mtu"), settings.mtu},
             {QStringLiteral("gateway"), strings({QStringLiteral("172.19.0.1/30"),
                                                   QStringLiteral("fdfe:dcba:9876::1/126")})},
             {QStringLiteral("autoSystemRoutingTable"), strings({QStringLiteral("0.0.0.0/0"),
                                                                  QStringLiteral("::/0")})},
             {QStringLiteral("autoOutboundsInterface"), networkInterface.isEmpty()
                  ? QStringLiteral("auto") : networkInterface},
         }},
        {QStringLiteral("sniffing"), QJsonObject{
             {QStringLiteral("enabled"), true},
             {QStringLiteral("destOverride"), strings({QStringLiteral("http"),
                                                         QStringLiteral("tls"),
                                                         QStringLiteral("quic")})},
             {QStringLiteral("routeOnly"), false},
         }},
    };
    const QJsonObject socksInbound{
        {QStringLiteral("tag"), QStringLiteral("socks-in")},
        {QStringLiteral("protocol"), QStringLiteral("socks")},
        {QStringLiteral("listen"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), LocalSocksPort},
        {QStringLiteral("settings"), QJsonObject{
             {QStringLiteral("auth"), QStringLiteral("noauth")},
             {QStringLiteral("udp"), false},
         }},
    };

    QJsonArray outbounds{buildProxyOutbound(profile, settings.forceIpv4, networkInterface)};
    outbounds.append(QJsonObject{
        {QStringLiteral("tag"), QStringLiteral("direct")},
        {QStringLiteral("protocol"), QStringLiteral("freedom")},
    });
    outbounds.append(QJsonObject{
        {QStringLiteral("tag"), QStringLiteral("blocked")},
        {QStringLiteral("protocol"), QStringLiteral("blackhole")},
    });
    QJsonArray dnsOutboundRules;
    if (settings.forceIpv4) {
        dnsOutboundRules.append(QJsonObject{
            {QStringLiteral("action"), QStringLiteral("return")},
            {QStringLiteral("qType"), 28},
            {QStringLiteral("rCode"), 0},
        });
    }
    outbounds.append(QJsonObject{
        {QStringLiteral("tag"), QStringLiteral("dns-out")},
        {QStringLiteral("protocol"), QStringLiteral("dns")},
        {QStringLiteral("settings"), QJsonObject{
             {QStringLiteral("rules"), dnsOutboundRules},
         }},
    });

    QJsonArray rules{
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("field")},
            {QStringLiteral("network"), QStringLiteral("tcp,udp")},
            {QStringLiteral("port"), QStringLiteral("53")},
            {QStringLiteral("outboundTag"), QStringLiteral("dns-out")},
        },
        QJsonObject{
            {QStringLiteral("type"), QStringLiteral("field")},
            {QStringLiteral("ip"), strings({
                 QStringLiteral("0.0.0.0/8"), QStringLiteral("10.0.0.0/8"),
                 QStringLiteral("100.64.0.0/10"), QStringLiteral("127.0.0.0/8"),
                 QStringLiteral("169.254.0.0/16"), QStringLiteral("172.16.0.0/12"),
                 QStringLiteral("192.168.0.0/16"), QStringLiteral("224.0.0.0/4"),
                 QStringLiteral("240.0.0.0/4")})},
            {QStringLiteral("outboundTag"), QStringLiteral("direct")},
        },
    };
    if (settings.forceIpv4) {
        rules.insert(1, QJsonObject{
            {QStringLiteral("type"), QStringLiteral("field")},
            {QStringLiteral("ip"), strings({QStringLiteral("::/0")})},
            {QStringLiteral("outboundTag"), QStringLiteral("blocked")},
        });
    }
    if (settings.blockQuic) {
        rules.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("field")},
            {QStringLiteral("network"), QStringLiteral("udp")},
            {QStringLiteral("port"), QStringLiteral("443")},
            {QStringLiteral("outboundTag"), QStringLiteral("blocked")},
        });
    }

    return {
        {QStringLiteral("log"), QJsonObject{{QStringLiteral("loglevel"), QStringLiteral("warning")}}},
        {QStringLiteral("dns"), QJsonObject{
             {QStringLiteral("servers"), QJsonArray{
                  QJsonObject{
                      {QStringLiteral("address"), QStringLiteral("localhost")},
                      {QStringLiteral("domains"), strings({
                           QStringLiteral("full:%1").arg(profile.server),
                       })},
                      {QStringLiteral("queryStrategy"), settings.forceIpv4
                           ? QStringLiteral("UseIPv4") : QStringLiteral("UseIP")},
                      {QStringLiteral("skipFallback"), true},
                  },
                  QJsonObject{
                      {QStringLiteral("address"), QStringLiteral("https://1.1.1.1/dns-query")},
                      {QStringLiteral("queryStrategy"), settings.forceIpv4
                           ? QStringLiteral("UseIPv4") : QStringLiteral("UseIP")},
                  },
              }},
             {QStringLiteral("queryStrategy"), settings.forceIpv4
                  ? QStringLiteral("UseIPv4") : QStringLiteral("UseIP")},
         }},
        {QStringLiteral("inbounds"), QJsonArray{tunInbound, socksInbound}},
        {QStringLiteral("outbounds"), outbounds},
        {QStringLiteral("routing"), QJsonObject{
             {QStringLiteral("domainStrategy"), QStringLiteral("AsIs")},
             {QStringLiteral("rules"), rules},
         }},
    };
}

QJsonObject XrayConfigBuilder::buildProxyOutbound(const VlessProfile &profile,
                                                  bool forceIpv4,
                                                  const QString &networkInterface)
{
    QJsonObject settings{
        {QStringLiteral("address"), profile.server},
        {QStringLiteral("port"), static_cast<int>(profile.serverPort)},
        {QStringLiteral("id"), profile.uuid},
        {QStringLiteral("encryption"), QStringLiteral("none")},
    };
    if (!profile.flow.isEmpty()) {
        settings.insert(QStringLiteral("flow"), profile.flow);
    }

    QJsonObject streamSettings{
        {QStringLiteral("method"), transportMethod(profile.transport)},
        {QStringLiteral("security"), profile.security},
    };
    QJsonObject sockopt{
        {QStringLiteral("domainStrategy"), forceIpv4
             ? QStringLiteral("ForceIPv4") : QStringLiteral("UseIP")},
    };
    if (!networkInterface.isEmpty()) {
        sockopt.insert(QStringLiteral("interface"), networkInterface);
    }
    streamSettings.insert(QStringLiteral("sockopt"), sockopt);

    if (profile.security == QStringLiteral("tls")) {
        QJsonObject tls{
            {QStringLiteral("serverName"), profile.serverName.isEmpty() ? profile.server : profile.serverName},
            {QStringLiteral("fingerprint"), profile.fingerprint},
            {QStringLiteral("allowInsecure"), profile.allowInsecure},
        };
        if (!profile.alpn.isEmpty()) {
            tls.insert(QStringLiteral("alpn"), QJsonArray::fromStringList(profile.alpn));
        }
        streamSettings.insert(QStringLiteral("tlsSettings"), tls);
    } else if (profile.security == QStringLiteral("reality")) {
        streamSettings.insert(QStringLiteral("realitySettings"), QJsonObject{
            {QStringLiteral("serverName"), profile.serverName},
            {QStringLiteral("fingerprint"), profile.fingerprint},
            {QStringLiteral("password"), profile.publicKey},
            {QStringLiteral("shortId"), profile.shortId},
            {QStringLiteral("spiderX"), QStringLiteral("")},
        });
    }

    if (profile.transport == QStringLiteral("ws")) {
        QJsonObject ws{{QStringLiteral("path"), profile.path.isEmpty() ? QStringLiteral("/") : profile.path}};
        if (!profile.host.isEmpty()) {
            ws.insert(QStringLiteral("headers"), QJsonObject{{QStringLiteral("Host"), profile.host}});
        }
        streamSettings.insert(QStringLiteral("wsSettings"), ws);
    } else if (profile.transport == QStringLiteral("grpc")) {
        streamSettings.insert(QStringLiteral("grpcSettings"),
                              QJsonObject{{QStringLiteral("serviceName"), profile.serviceName}});
    } else if (profile.transport == QStringLiteral("httpupgrade")) {
        QJsonObject upgrade{{QStringLiteral("path"), profile.path.isEmpty()
                                                    ? QStringLiteral("/") : profile.path}};
        if (!profile.host.isEmpty()) {
            upgrade.insert(QStringLiteral("host"), profile.host);
        }
        streamSettings.insert(QStringLiteral("httpupgradeSettings"), upgrade);
    } else if (profile.transport == QStringLiteral("xhttp")) {
        QJsonObject xhttp{{QStringLiteral("path"), profile.path.isEmpty()
                                                  ? QStringLiteral("/") : profile.path}};
        if (!profile.host.isEmpty()) {
            xhttp.insert(QStringLiteral("host"), profile.host);
        }
        if (!profile.xhttpMode.isEmpty()) {
            xhttp.insert(QStringLiteral("mode"), profile.xhttpMode);
        }
        streamSettings.insert(QStringLiteral("xhttpSettings"), xhttp);
    }

    return {
        {QStringLiteral("tag"), QStringLiteral("proxy")},
        {QStringLiteral("protocol"), QStringLiteral("vless")},
        {QStringLiteral("settings"), settings},
        {QStringLiteral("streamSettings"), streamSettings},
    };
}

bool XrayConfigBuilder::writeSecurely(const QJsonObject &config,
                                      const QString &path,
                                      QString *error)
{
    return SingBoxConfigBuilder::writeSecurely(config, path, error);
}

} // namespace lighttunnel
