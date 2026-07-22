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

void CoreUpdateManager::setCurrentCore(const QString &path)
{
    m_currentPath = path.trimmed();
    m_currentVersion = detectVersion(m_currentPath);
    emit currentVersionChanged(m_currentVersion);
}

void CoreUpdateManager::checkForUpdates(bool force)
{
    if (isBusy()) {
        return;
    }

    if (normalizedArchitecture().isEmpty()) {
        finishWithError(QStringLiteral("Автообновление sing-box пока поддерживает только x86_64 и arm64"));
        return;
    }

    if (!force && !m_currentPath.isEmpty()) {
        QSettings settings;
        const QDateTime lastCheck = settings.value(QStringLiteral("core/lastUpdateCheck")).toDateTime();
        if (lastCheck.isValid() && lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 60 * 60) {
            emit statusChanged(m_currentVersion.isEmpty()
                                   ? QStringLiteral("Автопроверка выполнена недавно")
                                   : QStringLiteral("sing-box %1 · недавно проверено").arg(m_currentVersion));
            return;
        }
    }

    requestMetadata();
}

QString CoreUpdateManager::detectVersion(const QString &corePath)
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
    const QRegularExpression expression(
        QStringLiteral("(?:^|\\n)sing-box version\\s+([^\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(output);
    return match.hasMatch() ? normalizedVersion(match.captured(1)) : QString();
}

std::optional<CoreRelease> CoreUpdateManager::parseRelease(const QByteArray &json, QString *error)
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
    const QString architecture = normalizedArchitecture();
    if (version.isEmpty() || architecture.isEmpty()
        || !QRegularExpression(QStringLiteral("^[0-9A-Za-z._-]+$")).match(version).hasMatch()) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось определить версию или архитектуру релиза");
        }
        return std::nullopt;
    }

    const QString expectedName = QStringLiteral("sing-box-%1-linux-%2.tar.gz")
                                     .arg(version, architecture);
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
        *error = QStringLiteral("Для архитектуры %1 не найден официальный Linux-архив sing-box")
                     .arg(architecture);
    }
    return std::nullopt;
}

QString CoreUpdateManager::managedCoreDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("core"));
}

void CoreUpdateManager::requestMetadata()
{
    m_metadata.clear();
    m_operation = Operation::Metadata;
    emit statusChanged(QStringLiteral("Проверяем обновления sing-box…"));

    QNetworkRequest request(QUrl(QStringLiteral(
        "https://api.github.com/repos/SagerNet/sing-box/releases/latest")));
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
        finishWithError(QStringLiteral("Не удалось проверить обновление sing-box: %1").arg(details));
        return;
    }
    if (m_metadata.size() > MaxMetadataSize) {
        clearReply();
        finishWithError(QStringLiteral("Ответ GitHub превышает допустимый размер"));
        return;
    }

    QString error;
    const auto release = parseRelease(m_metadata, &error);
    clearReply();
    if (!release.has_value()) {
        finishWithError(error);
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("core/lastUpdateCheck"), QDateTime::currentDateTimeUtc());
    if (!m_currentVersion.isEmpty() && !isNewer(release->version, m_currentVersion)) {
        emit statusChanged(QStringLiteral("sing-box %1 · актуальная версия").arg(m_currentVersion));
        return;
    }
    beginDownload(*release);
}

void CoreUpdateManager::beginDownload(const CoreRelease &release)
{
    m_release = release;
    m_downloadedBytes = 0;
    m_archive = std::make_unique<QTemporaryFile>(
        QDir::temp().filePath(QStringLiteral("lighttunnel-core-XXXXXX.tar.gz")));
    m_archive->setAutoRemove(true);
    if (!m_archive->open()) {
        finishWithError(QStringLiteral("Не удалось создать временный файл для обновления"));
        return;
    }

    m_operation = Operation::Download;
    emit statusChanged(QStringLiteral("Скачиваем sing-box %1…").arg(release.version));
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
        finishWithError(QStringLiteral("Не удалось скачать sing-box: %1").arg(details));
        return;
    }
    clearReply();

    m_archive->flush();
    m_archive->close();
    if (m_downloadedBytes != m_release.size) {
        m_archive.reset();
        finishWithError(QStringLiteral("Размер скачанного архива sing-box не совпадает с метаданными"));
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
        finishWithError(QStringLiteral("SHA-256 скачанного sing-box не совпадает с GitHub Release"));
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
    emit progressChanged(100);
    emit currentVersionChanged(m_currentVersion);
    emit coreInstalled(installedPath, m_currentVersion);
    emit statusChanged(QStringLiteral("sing-box %1 · обновлено").arg(m_currentVersion));
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
            *error = QStringLiteral("Не удалось прочитать официальный архив sing-box");
        }
        return false;
    }

    QString member;
    const QStringList entries = QString::fromUtf8(list.readAllStandardOutput())
                                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        if (entry.endsWith(QStringLiteral("/sing-box")) && !entry.startsWith(QLatin1Char('/'))
            && !entry.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            if (!member.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("В архиве обнаружено несколько файлов sing-box");
                }
                return false;
            }
            member = entry;
        }
    }
    if (member.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("В архиве отсутствует исполняемый файл sing-box");
        }
        return false;
    }

    QTemporaryDir extraction;
    if (!extraction.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось создать каталог распаковки sing-box");
        }
        return false;
    }
    QProcess extract;
    extract.start(tarPath, {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"),
                            extraction.path(), member});
    if (!extract.waitForFinished(60000) || extract.exitCode() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось распаковать sing-box");
        }
        return false;
    }

    const QString sourcePath = QDir(extraction.path()).filePath(member);
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile() || sourceInfo.isSymLink() || sourceInfo.size() <= 0
        || sourceInfo.size() > MaxBinarySize) {
        if (error != nullptr) {
            *error = QStringLiteral("Распакованный файл sing-box не прошёл проверку");
        }
        return false;
    }

    const QString directory = managedCoreDirectory();
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
        QStringLiteral("sing-box-%1").arg(m_release.version));
    if (!copyFileSecurely(sourcePath, destination, error)) {
        return false;
    }

    const QString detectedVersion = detectVersion(destination);
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
