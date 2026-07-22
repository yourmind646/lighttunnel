#include "core/profilerepository.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace lighttunnel {

QString ProfileRepository::filePath() const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("profiles.json"));
}

QVector<VlessProfile> ProfileRepository::load(QString *error) const
{
    QFile file(filePath());
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось открыть хранилище профилей: %1").arg(file.errorString());
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("Хранилище профилей повреждено: %1").arg(parseError.errorString());
        }
        return {};
    }

    QVector<VlessProfile> profiles;
    const QJsonArray array = document.array();
    profiles.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        QString profileError;
        const auto profile = VlessProfile::fromJson(value.toObject(), &profileError);
        if (profile.has_value()) {
            profiles.push_back(*profile);
        }
    }
    return profiles;
}

bool ProfileRepository::save(const QVector<VlessProfile> &profiles, QString *error) const
{
    const QFileInfo info(filePath());
    if (!QDir().mkpath(info.absolutePath())) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать каталог конфигурации");
        }
        return false;
    }

    QJsonArray array;
    for (const VlessProfile &profile : profiles) {
        array.append(profile.toJson());
    }

    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось сохранить профили: %1").arg(file.errorString());
        }
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(QJsonDocument(array).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("Ошибка записи профилей: %1").arg(file.errorString());
        }
        return false;
    }
    QFile::setPermissions(filePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

} // namespace lighttunnel
