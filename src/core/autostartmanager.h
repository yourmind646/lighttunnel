#pragma once

#include <QString>

namespace lighttunnel {

class AutostartManager final {
public:
    [[nodiscard]] static bool setEnabled(bool enabled, QString *error = nullptr);
    [[nodiscard]] static bool isEnabled();
    [[nodiscard]] static QString desktopFilePath();
};

} // namespace lighttunnel
