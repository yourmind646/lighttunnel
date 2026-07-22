#include "core/coreupdatemanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QVersionNumber>

namespace lighttunnel {
namespace {

constexpr qint64 MaxMetadataSize = 4 * 1024 * 1024;
constexpr qint64 MaxArchiveSize = 96 * 1024 * 1024;
constexpr qint64 MaxBinarySize = 160 * 1024 * 1024;

QString normalizedArchitecture()
{
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")) {
        return QStringLiteral("amd64");
    }
    if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64")) {
        return QStringLiteral("arm64");
    }
    return {};
}

QString releaseArchitecture(CoreType type)
{
    const QString architecture = normalizedArchitecture();
    if (type == CoreType::SingBox) {
        return architecture;
    }
    if (architecture == QStringLiteral("amd64")) {
        return QStringLiteral("64");
    }
    if (architecture == QStringLiteral("arm64")) {
        return QStringLiteral("arm64-v8a");
    }
    return {};
}

QString binaryName(CoreType type)
{
    return type == CoreType::Xray ? QStringLiteral("xray") : QStringLiteral("sing-box");
}

QString metadataUrl(CoreType type)
{
    return type == CoreType::Xray
        ? QStringLiteral("https://api.github.com/repos/XTLS/Xray-core/releases/latest")
        : QStringLiteral("https://api.github.com/repos/SagerNet/sing-box/releases/latest");
}

QString expectedAssetName(CoreType type, const QString &version)
{
    const QString architecture = releaseArchitecture(type);
    return type == CoreType::Xray
        ? QStringLiteral("Xray-linux-%1.zip").arg(architecture)
        : QStringLiteral("sing-box-%1-linux-%2.tar.gz").arg(version, architecture);
}

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}

bool copyFileSecurely(const QString &sourcePath, const QString &destinationPath, QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось открыть распакованное ядро: %1").arg(source.errorString());
        }
        return false;
    }

    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось подготовить файл ядра: %1").arg(destination.errorString());
        }
        return false;
    }

    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFile::NoError) {
            if (error != nullptr) {
                *error = QStringLiteral("Ошибка чтения распакованного ядра");
            }
            destination.cancelWriting();
            return false;
        }
        if (destination.write(chunk) != chunk.size()) {
            if (error != nullptr) {
                *error = QStringLiteral("Ошибка записи ядра: %1").arg(destination.errorString());
            }
            destination.cancelWriting();
            return false;
        }
    }

    if (!destination.commit()
        || !QFile::setPermissions(destinationPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось зафиксировать безопасные права нового ядра");
        }
        return false;
    }
    return true;
}

} // namespace

CoreUpdateManager::CoreUpdateManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

CoreUpdateManager::~CoreUpdateManager() = default;

void CoreUpdateManager::setCore(CoreType type, const QString &path)
{
    if (isBusy()) {
        return;
    }
    m_coreType = type;
    m_currentPath = path.trimmed();
    m_currentVersion = detectVersion(m_currentPath, m_coreType);
    emit currentVersionChanged(m_currentVersion);
}

void CoreUpdateManager::checkForUpdates(bool force)
{
    if (isBusy()) {
        return;
    }

    if (normalizedArchitecture().isEmpty()) {
        finishWithError(QStringLiteral("Автообновление %1 пока поддерживает только x86_64 и arm64")
                            .arg(coreDisplayName(m_coreType)));
        return;
    }

    if (!force && !m_currentPath.isEmpty()) {
        QSettings settings;
        const QDateTime lastCheck = settings.value(
            QStringLiteral("core/%1/lastUpdateCheck").arg(coreTypeKey(m_coreType))).toDateTime();
        if (lastCheck.isValid() && lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 60 * 60) {
            emit statusChanged(m_currentVersion.isEmpty()
                                   ? QStringLiteral("Автопроверка выполнена недавно")
                                   : QStringLiteral("%1 %2 · недавно проверено")
                                         .arg(coreDisplayName(m_coreType), m_currentVersion));
            return;
        }
    }

    requestMetadata();
}

QString CoreUpdateManager::detectVersion(const QString &corePath, CoreType type)
{
    const QFileInfo info(corePath);
    if (!info.isFile() || !info.isExecutable()) {
        return {};
    }

    QProcess process;
    process.start(corePath, {QStringLiteral("version")});
    if (!process.waitForStarted(2000) || !process.waitForFinished(5000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput()
                                              + process.readAllStandardError());
    const QString pattern = type == CoreType::Xray
        ? QStringLiteral("(?:^|\\n)Xray\\s+([^\\s]+)")
        : QStringLiteral("(?:^|\\n)sing-box version\\s+([^\\s]+)");
    const QRegularExpression expression(
        pattern,
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(output);
    return match.hasMatch() ? normalizedVersion(match.captured(1)) : QString();
}

std::optional<CoreRelease> CoreUpdateManager::parseRelease(const QByteArray &json,
                                                           CoreType type,
                                                           QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("GitHub вернул некорректные метаданные релиза");
        }
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("draft")).toBool()
        || root.value(QStringLiteral("prerelease")).toBool()) {
        if (error != nullptr) {
            *error = QStringLiteral("Последний релиз не является стабильным");
        }
        return std::nullopt;
    }

    const QString version = normalizedVersion(root.value(QStringLiteral("tag_name")).toString());
    const QString architecture = releaseArchitecture(type);
    if (version.isEmpty() || architecture.isEmpty()
        || !QRegularExpression(QStringLiteral("^[0-9A-Za-z._-]+$")).match(version).hasMatch()) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось определить версию или архитектуру релиза");
        }
        return std::nullopt;
    }

    const QString expectedName = expectedAssetName(type, version);
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString() != expectedName) {
            continue;
        }

        const QString digest = asset.value(QStringLiteral("digest")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        const qint64 size = asset.value(QStringLiteral("size")).toInteger();
        const QByteArray sha256 = digest.startsWith(QStringLiteral("sha256:"))
            ? digest.sliced(7).toLatin1().toLower()
            : QByteArray();
        const bool validUrl = url.scheme() == QStringLiteral("https")
            && url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0;
        if (!validUrl || sha256.size() != 64 || size <= 0 || size > MaxArchiveSize) {
            if (error != nullptr) {
                *error = QStringLiteral("У релиза отсутствуют доверенные SHA-256 метаданные");
            }
            return std::nullopt;
        }

        return CoreRelease{version, expectedName, url, sha256, size};
    }

    if (error != nullptr) {
        *error = QStringLiteral("Для архитектуры %1 не найден официальный Linux-архив %2")
                     .arg(architecture, coreDisplayName(type));
    }
    return std::nullopt;
}

QString CoreUpdateManager::managedCoreDirectory(CoreType type)
{
    return AppSettings::managedCoreDirectory(type);
}

void CoreUpdateManager::requestMetadata()
{
    m_metadata.clear();
    m_operation = Operation::Metadata;
    emit statusChanged(QStringLiteral("Проверяем обновления %1…")
                           .arg(coreDisplayName(m_coreType)));

    QNetworkRequest request{QUrl(metadataUrl(m_coreType))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "LightTunnel/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        m_metadata += m_reply->readAll();
        if (m_metadata.size() > MaxMetadataSize) {
            m_reply->abort();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &CoreUpdateManager::handleMetadataFinished);
}

void CoreUpdateManager::handleMetadataFinished()
{
    m_metadata += m_reply->readAll();
    if (m_reply->error() != QNetworkReply::NoError) {
        const QString details = m_reply->errorString();
        clearReply();
        finishWithError(QStringLiteral("Не удалось проверить обновление %1: %2")
                            .arg(coreDisplayName(m_coreType), details));
        return;
    }
    if (m_metadata.size() > MaxMetadataSize) {
        clearReply();
        finishWithError(QStringLiteral("Ответ GitHub превышает допустимый размер"));
        return;
    }

    QString error;
    const auto release = parseRelease(m_metadata, m_coreType, &error);
    clearReply();
    if (!release.has_value()) {
        finishWithError(error);
        return;
    }

    const QString managedPrefix = QDir(managedCoreDirectory(m_coreType)).absolutePath()
        + QDir::separator();
    const bool currentIsManaged = QFileInfo(m_currentPath).absoluteFilePath().startsWith(managedPrefix);
    if (currentIsManaged && !m_currentVersion.isEmpty()
        && !isNewer(release->version, m_currentVersion)) {
        QSettings settings;
        settings.setValue(QStringLiteral("core/%1/lastUpdateCheck").arg(coreTypeKey(m_coreType)),
                          QDateTime::currentDateTimeUtc());
        emit statusChanged(QStringLiteral("%1 %2 · актуальная версия")
                               .arg(coreDisplayName(m_coreType), m_currentVersion));
        return;
    }
    beginDownload(*release);
}

void CoreUpdateManager::beginDownload(const CoreRelease &release)
{
    m_release = release;
    m_downloadedBytes = 0;
    m_archive = std::make_unique<QTemporaryFile>(
        QDir::temp().filePath(QStringLiteral("lighttunnel-core-XXXXXX.archive")));
    m_archive->setAutoRemove(true);
    if (!m_archive->open()) {
        finishWithError(QStringLiteral("Не удалось создать временный файл для обновления"));
        return;
    }

    m_operation = Operation::Download;
    emit statusChanged(QStringLiteral("Скачиваем %1 %2…")
                           .arg(coreDisplayName(m_coreType), release.version));
    emit progressChanged(0);

    QNetworkRequest request(release.downloadUrl);
    request.setRawHeader("Accept", "application/octet-stream");
    request.setRawHeader("User-Agent", "LightTunnel/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &CoreUpdateManager::consumeDownloadData);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 expected = total > 0 ? total : m_release.size;
        if (expected > 0) {
            emit progressChanged(static_cast<int>(
                qBound<qint64>(qint64(0), received * 100 / expected, qint64(100))));
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &CoreUpdateManager::handleDownloadFinished);
}

void CoreUpdateManager::consumeDownloadData()
{
    const QByteArray data = m_reply->readAll();
    m_downloadedBytes += data.size();
    if (m_downloadedBytes > MaxArchiveSize || m_archive->write(data) != data.size()) {
        m_reply->abort();
    }
}

void CoreUpdateManager::handleDownloadFinished()
{
    consumeDownloadData();
    if (m_reply->error() != QNetworkReply::NoError) {
        const QString details = m_reply->errorString();
        clearReply();
        m_archive.reset();
        finishWithError(QStringLiteral("Не удалось скачать %1: %2")
                            .arg(coreDisplayName(m_coreType), details));
        return;
    }
    clearReply();

    m_archive->flush();
    m_archive->close();
    if (m_downloadedBytes != m_release.size) {
        m_archive.reset();
        finishWithError(QStringLiteral("Размер скачанного архива %1 не совпадает с метаданными")
                            .arg(coreDisplayName(m_coreType)));
        return;
    }

    QFile archiveFile(m_archive->fileName());
    if (!archiveFile.open(QIODevice::ReadOnly)) {
        m_archive.reset();
        finishWithError(QStringLiteral("Не удалось проверить скачанный архив"));
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!archiveFile.atEnd()) {
        hash.addData(archiveFile.read(1024 * 1024));
    }
    if (hash.result().toHex().toLower() != m_release.sha256) {
        m_archive.reset();
        finishWithError(QStringLiteral("SHA-256 скачанного %1 не совпадает с GitHub Release")
                            .arg(coreDisplayName(m_coreType)));
        return;
    }

    QString installedPath;
    QString error;
    if (!installDownloadedCore(&installedPath, &error)) {
        m_archive.reset();
        finishWithError(error);
        return;
    }
    m_archive.reset();
    m_currentPath = installedPath;
    m_currentVersion = m_release.version;
    QSettings settings;
    settings.setValue(QStringLiteral("core/%1/lastUpdateCheck").arg(coreTypeKey(m_coreType)),
                      QDateTime::currentDateTimeUtc());
    emit progressChanged(100);
    emit currentVersionChanged(m_currentVersion);
    emit coreInstalled(installedPath, m_currentVersion);
    emit statusChanged(QStringLiteral("%1 %2 · обновлено")
                           .arg(coreDisplayName(m_coreType), m_currentVersion));
}

bool CoreUpdateManager::installDownloadedCore(QString *installedPath, QString *error)
{
    const QString archivePath = m_archive->fileName();
    const QString tarPath = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (tarPath.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Для распаковки обновления требуется bsdtar (пакет libarchive)");
        }
        return false;
    }

    QProcess list;
    list.start(tarPath, {QStringLiteral("-tf"), archivePath});
    if (!list.waitForFinished(20000) || list.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось прочитать официальный архив %1")
                         .arg(coreDisplayName(m_coreType));
        }
        return false;
    }

    QString member;
    const QStringList entries = QString::fromUtf8(list.readAllStandardOutput())
                                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString executable = binaryName(m_coreType);
    for (const QString &entry : entries) {
        const bool expectedMember = m_coreType == CoreType::Xray
            ? entry == executable
            : entry.endsWith(QStringLiteral("/") + executable);
        if (expectedMember && !entry.startsWith(QLatin1Char('/'))
            && !entry.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            if (!member.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("В архиве обнаружено несколько файлов %1")
                                 .arg(executable);
                }
                return false;
            }
            member = entry;
        }
    }
    if (member.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("В архиве отсутствует исполняемый файл %1").arg(executable);
        }
        return false;
    }

    QTemporaryDir extraction;
    if (!extraction.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать каталог распаковки %1").arg(executable);
        }
        return false;
    }
    QProcess extract;
    extract.start(tarPath, {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"),
                            extraction.path(), member});
    if (!extract.waitForFinished(60000) || extract.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось распаковать %1").arg(executable);
        }
        return false;
    }

    const QString sourcePath = QDir(extraction.path()).filePath(member);
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile() || sourceInfo.isSymLink() || sourceInfo.size() <= 0
        || sourceInfo.size() > MaxBinarySize) {
        if (error != nullptr) {
            *error = QStringLiteral("Распакованный файл %1 не прошёл проверку").arg(executable);
        }
        return false;
    }

    const QString directory = managedCoreDirectory(m_coreType);
    if (!QDir().mkpath(directory)
        || !QFile::setPermissions(directory,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать защищённый каталог ядра");
        }
        return false;
    }
    const QString destination = QDir(directory).filePath(
        QStringLiteral("%1-%2").arg(executable, m_release.version));
    if (!copyFileSecurely(sourcePath, destination, error)) {
        return false;
    }

    const QString detectedVersion = detectVersion(destination, m_coreType);
    if (detectedVersion != m_release.version) {
        QFile::remove(destination);
        if (error != nullptr) {
            *error = QStringLiteral("Версия распакованного ядра не совпадает с GitHub Release");
        }
        return false;
    }
    if (installedPath != nullptr) {
        *installedPath = destination;
    }
    return true;
}

void CoreUpdateManager::finishWithError(const QString &message)
{
    m_operation = Operation::None;
    emit statusChanged(QStringLiteral("Ошибка обновления"));
    emit errorOccurred(message);
}

void CoreUpdateManager::clearReply()
{
    if (m_reply != nullptr) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_operation = Operation::None;
}

bool CoreUpdateManager::isNewer(const QString &candidate, const QString &current)
{
    const QVersionNumber candidateVersion = QVersionNumber::fromString(candidate);
    const QVersionNumber currentVersion = QVersionNumber::fromString(current);
    if (!candidateVersion.isNull() && !currentVersion.isNull()) {
        return QVersionNumber::compare(candidateVersion, currentVersion) > 0;
    }
    return candidate != current;
}

} // namespace lighttunnel
