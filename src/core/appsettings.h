#pragma once

#include <QString>

namespace lighttunnel {

enum class CoreType {
    SingBox,
    Xray,
};

[[nodiscard]] QString coreTypeKey(CoreType type);
[[nodiscard]] QString coreDisplayName(CoreType type);
[[nodiscard]] CoreType coreTypeFromKey(const QString &key);

struct AppSettings final {
    CoreType coreType{CoreType::SingBox};
    QString singBoxPath;
    QString xrayPath;
    QString networkInterface;
    QString tunStack{QStringLiteral("system")};
    int mtu{1500};
    bool blockQuic{false};
    bool forceIpv4{true};
    bool startMinimized{false};
    bool autoConnect{false};
    bool autostart{false};
    bool autoUpdateCore{true};
    QString lastProfileId;

    [[nodiscard]] static AppSettings load();
    void save() const;

    [[nodiscard]] QString corePath() const;
    void setCorePath(const QString &path);

    [[nodiscard]] static QString discoverCore(CoreType type);
    [[nodiscard]] static QString discoverDefaultInterface();
    [[nodiscard]] QString effectiveInterface() const;
    [[nodiscard]] static QString runtimeDirectory();
    [[nodiscard]] static QString managedCoreDirectory(CoreType type);
};

} // namespace lighttunnel
