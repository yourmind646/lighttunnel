#pragma once

#include "core/vlessprofile.h"

#include <QString>
#include <QVector>

namespace lighttunnel {

class ProfileRepository final {
public:
    [[nodiscard]] QVector<VlessProfile> load(QString *error = nullptr) const;
    [[nodiscard]] bool save(const QVector<VlessProfile> &profiles, QString *error = nullptr) const;
    [[nodiscard]] QString filePath() const;
};

} // namespace lighttunnel
