#pragma once

#include <QString>

namespace lighttunnel {

struct AppSettings final {
    QString corePath;
    QString networkInterface;
    QString tunStack{QStringLiteral("system")};
    int mtu{1500};
    bool blockQuic{false};
    bool startMinimized{false};
    bool autoConnect{false};
    bool autostart{false};
    bool autoUpdateCore{true};
    QString lastProfileId;

    [[nodiscard]] static AppSettings load();
    void save() const;

    [[nodiscard]] static QString discoverCore();
    [[nodiscard]] static QString discoverDefaultInterface();
    [[nodiscard]] QString effectiveInterface() const;
    [[nodiscard]] static QString runtimeDirectory();
    [[nodiscard]] static QString managedCoreDirectory();
};

} // namespace lighttunnel
