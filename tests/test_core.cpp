#include "core/appsettings.h"
#include "core/coreupdatemanager.h"
#include "core/singboxconfigbuilder.h"
#include "core/systemdcommandbuilder.h"
#include "core/vlessprofile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using lighttunnel::AppSettings;
using lighttunnel::CoreUpdateManager;
using lighttunnel::SingBoxConfigBuilder;
using lighttunnel::SystemdCommandBuilder;
using lighttunnel::VlessProfile;

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesRealityUri();
    void parsesWebSocketUri();
    void rejectsUnsupportedTransport();
    void bindsEveryNetworkOutbound();
    void quicPolicyIsExplicit();
    void systemdServiceUsesCallingUserWithNetworkCapabilities();
    void parsesVerifiedCoreRelease();
    void generatedConfigPassesSingBoxCheck();
};

void CoreTests::parsesRealityUri()
{
    const QString uri = QStringLiteral(
        "vless://11111111-2222-3333-4444-555555555555@example.com:443"
        "?security=reality&flow=xtls-rprx-vision&sni=example.com&fp=chrome"
        "&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&sid=0123456789abcdef&type=tcp#Demo");
    QString error;
    const auto profile = VlessProfile::fromUri(uri, &error);
    QVERIFY2(profile.has_value(), qPrintable(error));
    QCOMPARE(profile->name, QStringLiteral("Demo"));
    QCOMPARE(profile->server, QStringLiteral("example.com"));
    QCOMPARE(profile->serverPort, quint16(443));
    QCOMPARE(profile->security, QStringLiteral("reality"));
    QCOMPARE(profile->flow, QStringLiteral("xtls-rprx-vision"));
}

void CoreTests::parsesWebSocketUri()
{
    const QString uri = QStringLiteral(
        "vless://11111111-2222-3333-4444-555555555555@example.com:8443"
        "?security=tls&sni=cdn.example.com&type=ws&path=%2Fvpn&host=edge.example.com"
        "&alpn=h2%2Chttp%2F1.1&ed=2048&eh=Sec-WebSocket-Protocol#WebSocket");
    const auto profile = VlessProfile::fromUri(uri);
    QVERIFY(profile.has_value());
    QCOMPARE(profile->transport, QStringLiteral("ws"));
    QCOMPARE(profile->path, QStringLiteral("/vpn"));
    QCOMPARE(profile->host, QStringLiteral("edge.example.com"));
    QCOMPARE(profile->alpn, QStringList({QStringLiteral("h2"), QStringLiteral("http/1.1")}));
    QCOMPARE(profile->maxEarlyData, 2048);
}

void CoreTests::rejectsUnsupportedTransport()
{
    const QString uri = QStringLiteral(
        "vless://11111111-2222-3333-4444-555555555555@example.com:443?type=xhttp#Unsupported");
    QString error;
    QVERIFY(!VlessProfile::fromUri(uri, &error).has_value());
    QVERIFY(error.contains(QStringLiteral("Xray-core")));
}

void CoreTests::bindsEveryNetworkOutbound()
{
    VlessProfile profile;
    profile.uuid = QStringLiteral("11111111-2222-3333-4444-555555555555");
    profile.server = QStringLiteral("example.com");
    AppSettings settings;
    const QJsonObject config = SingBoxConfigBuilder::build(profile, settings, QStringLiteral("wlan0"));
    const QJsonArray outbounds = config.value(QStringLiteral("outbounds")).toArray();
    QCOMPARE(outbounds.size(), 2);
    QCOMPARE(outbounds.at(0).toObject().value(QStringLiteral("bind_interface")).toString(),
             QStringLiteral("wlan0"));
    QCOMPARE(outbounds.at(1).toObject().value(QStringLiteral("bind_interface")).toString(),
             QStringLiteral("wlan0"));
}

void CoreTests::quicPolicyIsExplicit()
{
    VlessProfile profile;
    profile.uuid = QStringLiteral("11111111-2222-3333-4444-555555555555");
    profile.server = QStringLiteral("example.com");
    AppSettings settings;
    settings.blockQuic = false;
    QJsonArray rules = SingBoxConfigBuilder::build(profile, settings, QStringLiteral("wlan0"))
                           .value(QStringLiteral("route"))
                           .toObject()
                           .value(QStringLiteral("rules"))
                           .toArray();
    QCOMPARE(rules.size(), 3);

    settings.blockQuic = true;
    rules = SingBoxConfigBuilder::build(profile, settings, QStringLiteral("wlan0"))
                .value(QStringLiteral("route"))
                .toObject()
                .value(QStringLiteral("rules"))
                .toArray();
    QCOMPARE(rules.size(), 4);
    QCOMPARE(rules.last().toObject().value(QStringLiteral("action")).toString(), QStringLiteral("reject"));
}

void CoreTests::systemdServiceUsesCallingUserWithNetworkCapabilities()
{
    const QStringList arguments = SystemdCommandBuilder::startArguments(
        QStringLiteral("lighttunnel-core-1000.service"),
        QStringLiteral("/usr/bin/sing-box"),
        QStringLiteral("/home/user/.local/share/LightTunnel/runtime/config.json"),
        1000,
        1000);

    QVERIFY(arguments.contains(QStringLiteral("--property=User=1000")));
    QVERIFY(arguments.contains(QStringLiteral("--property=Group=1000")));
    QVERIFY(arguments.contains(QStringLiteral("--property=UMask=0077")));
    QVERIFY(arguments.contains(QStringLiteral(
        "--property=AmbientCapabilities=CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE")));
    QVERIFY(arguments.contains(QStringLiteral(
        "--property=CapabilityBoundingSet=CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE")));
    QVERIFY(!arguments.join(QLatin1Char(' ')).contains(QStringLiteral("CAP_DAC")));
}

void CoreTests::parsesVerifiedCoreRelease()
{
    const QByteArray json = R"json({
        "tag_name": "v1.13.14",
        "draft": false,
        "prerelease": false,
        "assets": [
            {
                "name": "sing-box-1.13.14-linux-amd64.tar.gz",
                "size": 23832905,
                "digest": "sha256:f48703461a15476951ac4967cdad339d986f4b8096b4eb3ff0829a500502d697",
                "browser_download_url": "https://github.com/SagerNet/sing-box/releases/download/v1.13.14/sing-box-1.13.14-linux-amd64.tar.gz"
            },
            {
                "name": "sing-box-1.13.14-linux-arm64.tar.gz",
                "size": 22028804,
                "digest": "sha256:4742df6a4314e8ecc41736849fca6d73b8f9e91b6e8b06ee794ff17ba180579e",
                "browser_download_url": "https://github.com/SagerNet/sing-box/releases/download/v1.13.14/sing-box-1.13.14-linux-arm64.tar.gz"
            }
        ]
    })json";

    QString error;
    const auto release = CoreUpdateManager::parseRelease(json, &error);
    QVERIFY2(release.has_value(), qPrintable(error));
    QCOMPARE(release->version, QStringLiteral("1.13.14"));
    QCOMPARE(release->sha256.size(), 64);
    QVERIFY(release->assetName.endsWith(QStringLiteral(".tar.gz")));
    QCOMPARE(release->downloadUrl.scheme(), QStringLiteral("https"));
}

void CoreTests::generatedConfigPassesSingBoxCheck()
{
    const QString core = AppSettings::discoverCore();
    if (core.isEmpty()) {
        QSKIP("sing-box is not installed");
    }

    VlessProfile profile;
    profile.uuid = QStringLiteral("11111111-2222-3333-4444-555555555555");
    profile.server = QStringLiteral("example.com");
    profile.security = QStringLiteral("reality");
    profile.serverName = QStringLiteral("example.com");
    profile.publicKey = QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    profile.shortId = QStringLiteral("0123456789abcdef");
    profile.flow = QStringLiteral("xtls-rprx-vision");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("config.json"));
    QString error;
    QVERIFY2(SingBoxConfigBuilder::writeSecurely(
                 SingBoxConfigBuilder::build(profile, AppSettings{}, QStringLiteral("wlan0")), path, &error),
             qPrintable(error));

    QProcess check;
    check.start(core, {QStringLiteral("check"), QStringLiteral("-c"), path});
    QVERIFY(check.waitForFinished(10000));
    const QByteArray details = check.readAllStandardError() + check.readAllStandardOutput();
    QVERIFY2(check.exitCode() == 0, qPrintable(QString::fromUtf8(details)));
}

QTEST_MAIN(CoreTests)
#include "test_core.moc"
