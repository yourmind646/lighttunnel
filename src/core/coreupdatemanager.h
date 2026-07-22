#pragma once

#include "core/appsettings.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryFile;

namespace lighttunnel {

struct CoreRelease final {
    QString version;
    QString assetName;
    QUrl downloadUrl;
    QByteArray sha256;
    qint64 size{};
};

class CoreUpdateManager final : public QObject {
    Q_OBJECT

public:
    explicit CoreUpdateManager(QObject *parent = nullptr);
    ~CoreUpdateManager() override;

    void setCore(CoreType type, const QString &path);
    void checkForUpdates(bool force = false);

    [[nodiscard]] QString currentPath() const { return m_currentPath; }
    [[nodiscard]] QString currentVersion() const { return m_currentVersion; }
    [[nodiscard]] CoreType coreType() const noexcept { return m_coreType; }
    [[nodiscard]] bool isBusy() const noexcept { return m_reply != nullptr; }

    [[nodiscard]] static QString detectVersion(const QString &corePath, CoreType type);
    [[nodiscard]] static bool supportsNativeXrayRouting(const QString &version);
    [[nodiscard]] static std::optional<CoreRelease> parseRelease(const QByteArray &json,
                                                                CoreType type,
                                                                QString *error = nullptr);
    [[nodiscard]] static QString managedCoreDirectory(CoreType type);

signals:
    void statusChanged(const QString &status);
    void progressChanged(int percent);
    void currentVersionChanged(const QString &version);
    void coreInstalled(const QString &path, const QString &version);
    void errorOccurred(const QString &message);

private:
    enum class Operation {
        None,
        Metadata,
        Download,
    };

    void requestMetadata();
    void handleMetadataFinished();
    void beginDownload(const CoreRelease &release);
    void consumeDownloadData();
    void handleDownloadFinished();
    [[nodiscard]] bool installDownloadedCore(QString *installedPath, QString *error);
    void finishWithError(const QString &message);
    void clearReply();
    [[nodiscard]] static bool isNewer(const QString &candidate, const QString &current);

    QNetworkAccessManager *m_network{};
    QNetworkReply *m_reply{};
    Operation m_operation{Operation::None};
    QString m_currentPath;
    QString m_currentVersion;
    QByteArray m_metadata;
    CoreRelease m_release;
    std::unique_ptr<QTemporaryFile> m_archive;
    qint64 m_downloadedBytes{};
    CoreType m_coreType{CoreType::SingBox};
};

} // namespace lighttunnel
