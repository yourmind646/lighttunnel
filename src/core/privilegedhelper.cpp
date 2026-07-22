#include "core/privilegedhelper.h"

#include "core/appsettings.h"
#include "core/systemdcommandbuilder.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>

#include <csignal>
#include <limits>

#include <pwd.h>
#include <sys/prctl.h>
#include <unistd.h>

namespace lighttunnel {
namespace {

constexpr qsizetype MaxDiagnosticSize = 8 * 1024;

bool parseUnsignedId(const QByteArray &text, quint32 *result)
{
    bool ok = false;
    const qulonglong value = text.toULongLong(&ok, 10);
    if (!ok || value == 0 || value > std::numeric_limits<quint32>::max()) {
        return false;
    }
    *result = static_cast<quint32>(value);
    return true;
}

bool validateCore(const QString &path, quint32 callerUid, QString *canonicalPath, QString *error)
{
    const QFileInfo info(path);
    const QFileDevice::Permissions permissions = info.permissions();
    if (!info.isAbsolute() || !info.exists() || !info.isFile() || info.isSymLink()
        || !info.isExecutable() || info.canonicalFilePath().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Исполняемый файл ядра не прошёл проверку");
        }
        return false;
    }
    if (info.ownerId() != callerUid && info.ownerId() != 0) {
        if (error != nullptr) {
            *error = QStringLiteral("Ядро должно принадлежать пользователю или root");
        }
        return false;
    }
    if (permissions.testFlag(QFileDevice::WriteGroup)
        || permissions.testFlag(QFileDevice::WriteOther)) {
        if (error != nullptr) {
            *error = QStringLiteral("Ядро доступно для небезопасной записи группе или другим пользователям");
        }
        return false;
    }
    *canonicalPath = info.canonicalFilePath();
    return true;
}

bool validateConfig(const QString &path, quint32 callerUid, QString *canonicalPath, QString *error)
{
    const QFileInfo info(path);
    const QFileDevice::Permissions permissions = info.permissions();
    if (!info.isAbsolute() || !info.exists() || !info.isFile() || info.isSymLink()
        || info.canonicalFilePath().isEmpty() || info.ownerId() != callerUid) {
        if (error != nullptr) {
            *error = QStringLiteral("Конфигурация должна быть обычным файлом текущего пользователя");
        }
        return false;
    }
    constexpr QFileDevice::Permissions forbidden = QFileDevice::ReadGroup
        | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
        | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if ((permissions & forbidden) != QFileDevice::Permissions{}) {
        if (error != nullptr) {
            *error = QStringLiteral("Конфигурация должна иметь приватные права доступа 0600");
        }
        return false;
    }
    *canonicalPath = info.canonicalFilePath();
    return true;
}

QJsonObject response(qint64 requestId, bool ok, const QString &message = {})
{
    QJsonObject object{{QStringLiteral("id"), requestId}, {QStringLiteral("ok"), ok}};
    if (!message.isEmpty()) {
        object.insert(QStringLiteral("message"), message);
    }
    return object;
}

void writeResponse(QFile *output, const QJsonObject &object)
{
    output->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    output->write("\n");
    output->flush();
}

QString runCommand(const PrivilegedCommand &command)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(command.program, command.arguments);
    if (!process.waitForStarted(3000)) {
        return QStringLiteral("Не удалось запустить системную команду: %1")
            .arg(process.errorString());
    }
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(1000);
        return QStringLiteral("Системная операция превысила 20 секунд");
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return {};
    }
    QByteArray details = process.readAllStandardError().trimmed();
    if (details.isEmpty()) {
        details = process.readAllStandardOutput().trimmed();
    }
    details.truncate(MaxDiagnosticSize);
    return details.isEmpty()
        ? QStringLiteral("Системная операция завершилась с кодом %1").arg(process.exitCode())
        : QString::fromUtf8(details);
}

} // namespace

std::optional<PrivilegedCommand> PrivilegedHelper::commandForRequest(
    const QJsonObject &request,
    quint32 callerUid,
    quint32 callerGid,
    QString *error)
{
    const QString operation = request.value(QStringLiteral("operation")).toString();
    const QString unitName = QStringLiteral("lighttunnel-core-%1.service").arg(callerUid);
    if (operation == QStringLiteral("stop")) {
        return PrivilegedCommand{
            QStringLiteral("/usr/bin/systemctl"),
            {QStringLiteral("stop"), unitName},
        };
    }
    if (operation != QStringLiteral("start")) {
        if (error != nullptr) {
            *error = QStringLiteral("Неизвестная привилегированная операция");
        }
        return std::nullopt;
    }

    const QString coreTypeName = request.value(QStringLiteral("coreType")).toString();
    if (coreTypeName != QStringLiteral("sing-box") && coreTypeName != QStringLiteral("xray")) {
        if (error != nullptr) {
            *error = QStringLiteral("Неизвестный тип ядра");
        }
        return std::nullopt;
    }

    QString corePath;
    QString configPath;
    if (!validateCore(request.value(QStringLiteral("corePath")).toString(), callerUid,
                      &corePath, error)
        || !validateConfig(request.value(QStringLiteral("configPath")).toString(), callerUid,
                           &configPath, error)) {
        return std::nullopt;
    }

    QStringList arguments = SystemdCommandBuilder::startArguments(
        unitName,
        corePath,
        configPath,
        callerUid,
        callerGid,
        coreTypeFromKey(coreTypeName));
    if (arguments.isEmpty() || arguments.takeFirst() != QStringLiteral("systemd-run")) {
        if (error != nullptr) {
            *error = QStringLiteral("Внутренняя ошибка построения systemd-команды");
        }
        return std::nullopt;
    }
    return PrivilegedCommand{QStringLiteral("/usr/bin/systemd-run"), arguments};
}

int PrivilegedHelper::run()
{
    if (::geteuid() != 0) {
        return 77;
    }
    quint32 callerUid = 0;
    if (!parseUnsignedId(qgetenv("PKEXEC_UID"), &callerUid)) {
        return 77;
    }
    const passwd *account = ::getpwuid(static_cast<uid_t>(callerUid));
    if (account == nullptr || account->pw_uid != callerUid) {
        return 77;
    }
    const quint32 callerGid = static_cast<quint32>(account->pw_gid);

    // The authorization exists only while the unprivileged GUI keeps this
    // process and its stdin pipe alive.
    if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || ::getppid() == 1) {
        return 77;
    }

    QFile input;
    QFile output;
    if (!input.open(stdin, QIODevice::ReadOnly)
        || !output.open(stdout, QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        return 74;
    }

    while (true) {
        QByteArray line = input.readLine(MaxRequestSize + 1);
        if (line.isEmpty()) {
            break;
        }
        if (!line.endsWith('\n') || line.size() > MaxRequestSize) {
            writeResponse(&output, response(0, false, QStringLiteral("Слишком большой запрос")));
            while (!line.endsWith('\n') && !line.isEmpty()) {
                line = input.readLine(MaxRequestSize + 1);
            }
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(&output, response(0, false, QStringLiteral("Некорректный запрос")));
            continue;
        }
        const QJsonObject request = document.object();
        const qint64 requestId = request.value(QStringLiteral("id")).toInteger();
        if (requestId <= 0) {
            writeResponse(&output, response(0, false, QStringLiteral("Некорректный ID запроса")));
            continue;
        }

        QString error;
        const auto command = commandForRequest(request, callerUid, callerGid, &error);
        if (!command.has_value()) {
            writeResponse(&output, response(requestId, false, error));
            continue;
        }
        error = runCommand(*command);
        writeResponse(&output, response(requestId, error.isEmpty(), error));
    }
    return 0;
}

} // namespace lighttunnel
