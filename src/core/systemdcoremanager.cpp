#include "core/systemdcoremanager.h"

#include "core/coreupdatemanager.h"
#include "core/singboxconfigbuilder.h"
#include "core/xrayconfigbuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>

#include <unistd.h>

namespace lighttunnel {

SystemdCoreManager::SystemdCoreManager(QObject *parent)
    : QObject(parent)
    , m_unitName(QStringLiteral("lighttunnel-core-%1.service").arg(static_cast<qulonglong>(::getuid())))
    , m_configPath(QDir(AppSettings::runtimeDirectory()).filePath(QStringLiteral("config.json")))
{
    m_stateTimer.setInterval(1500);
    connect(&m_stateTimer, &QTimer::timeout, this, &SystemdCoreManager::refreshState);

    connect(&m_helperProcess, &QProcess::started, this, &SystemdCoreManager::sendPendingRequest);
    connect(&m_helperProcess, &QProcess::readyReadStandardOutput,
            this, &SystemdCoreManager::consumeHelperOutput);
    connect(&m_helperProcess, &QProcess::readyReadStandardError, this, [this] {
        m_helperError += m_helperProcess.readAllStandardError();
        constexpr qsizetype maxErrorSize = 16 * 1024;
        if (m_helperError.size() > maxErrorSize) {
            m_helperError = m_helperError.right(maxErrorSize);
        }
    });
    connect(&m_helperProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                consumeHelperOutput();
                if (m_pendingRequestId == 0) {
                    return;
                }
                QString message = QString::fromUtf8(m_helperError).trimmed();
                if (message.isEmpty()) {
                    message = exitStatus == QProcess::NormalExit && exitCode == 126
                        ? QStringLiteral("Авторизация Polkit отменена")
                        : QStringLiteral("Привилегированный helper завершился с кодом %1")
                              .arg(exitCode);
                }
                failPendingRequest(message);
            });
    connect(&m_helperProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart && m_pendingRequestId != 0) {
                    failPendingRequest(QStringLiteral("Не удалось запустить Polkit helper: %1")
                                           .arg(m_helperProcess.errorString()));
                }
            });

    connect(&m_journalProcess, &QProcess::readyReadStandardOutput, this, [this] {
        const QString output = QString::fromUtf8(m_journalProcess.readAllStandardOutput());
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            emit logLine(line);
        }
    });
    connect(&m_journalProcess, &QProcess::readyReadStandardError, this, [this] {
        const QString output = QString::fromUtf8(m_journalProcess.readAllStandardError()).trimmed();
        if (!output.isEmpty()) {
            emit logLine(output);
        }
    });

    refreshState();
    m_stateTimer.start();
}

SystemdCoreManager::~SystemdCoreManager()
{
    stopJournalFollow();
    if (m_helperProcess.state() != QProcess::NotRunning) {
        m_helperProcess.closeWriteChannel();
        m_helperProcess.waitForFinished(1500);
    }
}

void SystemdCoreManager::connectTunnel(const VlessProfile &profile, const AppSettings &settings)
{
    if (m_state == State::Starting || m_state == State::Stopping || unitIsActive()) {
        emit errorOccurred(QStringLiteral("Туннель уже запущен или выполняется другая операция"));
        refreshState();
        return;
    }

    m_runningCoreType = settings.coreType;
    m_corePath = settings.corePath().trimmed();
    const QFileInfo coreInfo(m_corePath);
    if (!coreInfo.isFile() || !coreInfo.isExecutable()) {
        emit errorOccurred(QStringLiteral("Не найден исполняемый %1. Укажите путь в настройках.")
                               .arg(coreDisplayName(m_runningCoreType)));
        setState(State::Error);
        return;
    }
    if (m_runningCoreType == CoreType::Xray) {
        const QString version = CoreUpdateManager::detectVersion(m_corePath, CoreType::Xray);
        if (!CoreUpdateManager::supportsNativeXrayRouting(version)) {
            emit errorOccurred(
                QStringLiteral("Xray %1 не поддерживает надёжную автоматическую маршрутизацию "
                               "native TUN. Обновите Xray до 26.5.9 или новее кнопкой «Проверить».")
                    .arg(version.isEmpty() ? QStringLiteral("неизвестной версии") : version));
            setState(State::Error);
            return;
        }
    }

    const QString networkInterface = settings.effectiveInterface();
    if (networkInterface.isEmpty()) {
        emit errorOccurred(QStringLiteral("Не удалось определить основной сетевой интерфейс"));
        setState(State::Error);
        return;
    }

    QString error;
    if (m_runningCoreType == CoreType::SingBox && profile.transport == QStringLiteral("xhttp")) {
        emit errorOccurred(QStringLiteral("Профиль использует XHTTP. Выберите ядро Xray в настройках."));
        setState(State::Error);
        return;
    }
    const QJsonObject config = m_runningCoreType == CoreType::Xray
        ? XrayConfigBuilder::build(profile, settings, networkInterface)
        : SingBoxConfigBuilder::build(profile, settings, networkInterface);
    if (!SingBoxConfigBuilder::writeSecurely(config, m_configPath, &error)) {
        emit errorOccurred(error);
        setState(State::Error);
        return;
    }
    if (!validateConfig(m_corePath, m_runningCoreType, &error)) {
        emit errorOccurred(error);
        emit logLine(error);
        setState(State::Error);
        return;
    }

    emit logLine(QStringLiteral("Конфигурация проверена. Запускаю защищённый TUN…"));
    m_controlIsStart = true;
    runPrivileged(
        QJsonObject{
            {QStringLiteral("operation"), QStringLiteral("start")},
            {QStringLiteral("corePath"), m_corePath},
            {QStringLiteral("configPath"), m_configPath},
            {QStringLiteral("coreType"), coreTypeKey(m_runningCoreType)},
        },
        State::Starting);
}

void SystemdCoreManager::disconnectTunnel()
{
    if (!unitIsActive()) {
        stopJournalFollow();
        setState(State::Disconnected);
        return;
    }
    emit logLine(QStringLiteral("Останавливаю TUN…"));
    m_controlIsStart = false;
    runPrivileged(
        QJsonObject{{QStringLiteral("operation"), QStringLiteral("stop")}},
        State::Stopping);
}

void SystemdCoreManager::refreshState()
{
    if (m_pendingRequestId != 0) {
        return;
    }
    const bool active = unitIsActive();
    if (active && m_state != State::Connected) {
        setState(State::Connected);
        beginJournalFollow();
    } else if (!active && m_state == State::Connected) {
        stopJournalFollow();
        setState(State::Disconnected);
        emit logLine(QStringLiteral("%1 остановлен").arg(coreDisplayName(m_runningCoreType)));
    } else if (!active && m_state == State::Error) {
        setState(State::Disconnected);
    }
}

void SystemdCoreManager::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

void SystemdCoreManager::beginJournalFollow()
{
    if (m_journalProcess.state() != QProcess::NotRunning) {
        return;
    }
    m_journalProcess.start(QStringLiteral("journalctl"), {
        QStringLiteral("--unit=%1").arg(m_unitName),
        QStringLiteral("--follow"),
        QStringLiteral("--no-pager"),
        QStringLiteral("--output=cat"),
        QStringLiteral("--lines=30"),
    });
}

void SystemdCoreManager::stopJournalFollow()
{
    if (m_journalProcess.state() == QProcess::NotRunning) {
        return;
    }
    m_journalProcess.terminate();
    if (!m_journalProcess.waitForFinished(1000)) {
        m_journalProcess.kill();
        m_journalProcess.waitForFinished(1000);
    }
}

void SystemdCoreManager::runPrivileged(QJsonObject request, State transitionalState)
{
    if (m_pendingRequestId != 0) {
        emit errorOccurred(QStringLiteral("Другая системная операция ещё выполняется"));
        return;
    }

    if (m_nextRequestId <= 0) {
        m_nextRequestId = 1;
    }
    m_pendingRequestId = m_nextRequestId++;
    request.insert(QStringLiteral("id"), m_pendingRequestId);
    m_pendingRequest = request;
    m_pendingRequestSent = false;
    m_helperError.clear();
    setState(transitionalState);

    if (m_helperProcess.state() == QProcess::Running) {
        sendPendingRequest();
        return;
    }
    if (m_helperProcess.state() == QProcess::NotRunning) {
        m_helperOutput.clear();
        m_helperProcess.start(
            QStringLiteral("pkexec"),
            {QCoreApplication::applicationFilePath(), QStringLiteral("--privileged-helper")});
    }
}

void SystemdCoreManager::sendPendingRequest()
{
    if (m_pendingRequestId == 0 || m_pendingRequestSent
        || m_helperProcess.state() != QProcess::Running) {
        return;
    }
    QByteArray payload = QJsonDocument(m_pendingRequest).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (m_helperProcess.write(payload) != payload.size()) {
        failPendingRequest(QStringLiteral("Не удалось передать команду привилегированному helper"));
        return;
    }
    m_pendingRequestSent = true;
}

void SystemdCoreManager::consumeHelperOutput()
{
    m_helperOutput += m_helperProcess.readAllStandardOutput();
    constexpr qsizetype maxOutputSize = 128 * 1024;
    if (m_helperOutput.size() > maxOutputSize) {
        m_helperOutput.clear();
        failPendingRequest(QStringLiteral("Привилегированный helper вернул слишком большой ответ"));
        return;
    }

    qsizetype newline = -1;
    while ((newline = m_helperOutput.indexOf('\n')) >= 0) {
        const QByteArray line = m_helperOutput.first(newline);
        m_helperOutput.remove(0, newline + 1);

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            failPendingRequest(QStringLiteral("Привилегированный helper вернул некорректный ответ"));
            return;
        }
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("id")).toInteger() != m_pendingRequestId) {
            failPendingRequest(QStringLiteral("Нарушена последовательность ответов helper"));
            return;
        }
        finishPrivilegedRequest(
            object.value(QStringLiteral("ok")).toBool(),
            object.value(QStringLiteral("message")).toString());
        return;
    }
}

void SystemdCoreManager::finishPrivilegedRequest(bool ok, const QString &message)
{
    m_pendingRequestId = 0;
    m_pendingRequest = {};
    m_pendingRequestSent = false;
    m_helperError.clear();

    if (!ok) {
        const QString details = message.isEmpty()
            ? QStringLiteral("Привилегированная операция завершилась с ошибкой")
            : message;
        setState(State::Error);
        emit errorOccurred(details);
        emit logLine(QStringLiteral("Ошибка: %1").arg(details));
        return;
    }

    if (m_controlIsStart) {
        if (unitIsActive()) {
            setState(State::Connected);
            beginJournalFollow();
        } else {
            setState(State::Error);
            emit errorOccurred(QStringLiteral("%1 завершился сразу после запуска. Проверьте журнал.")
                                   .arg(coreDisplayName(m_runningCoreType)));
            beginJournalFollow();
        }
    } else {
        stopJournalFollow();
        setState(State::Disconnected);
    }
}

void SystemdCoreManager::failPendingRequest(const QString &message)
{
    if (m_pendingRequestId == 0) {
        return;
    }
    finishPrivilegedRequest(false, message);
}

bool SystemdCoreManager::validateConfig(const QString &corePath, CoreType type, QString *error)
{
    QProcess check;
    const QStringList arguments = type == CoreType::Xray
        ? QStringList{QStringLiteral("run"), QStringLiteral("-test"), QStringLiteral("-dump"),
                      QStringLiteral("-c"), m_configPath}
        : QStringList{QStringLiteral("check"), QStringLiteral("-c"), m_configPath};
    check.start(corePath, arguments);
    if (!check.waitForStarted(2000)) {
        if (error != nullptr) {
            *error = QStringLiteral("Не удалось запустить проверку %1: %2")
                         .arg(coreDisplayName(type), check.errorString());
        }
        return false;
    }
    if (!check.waitForFinished(10000)) {
        check.kill();
        if (error != nullptr) {
            *error = QStringLiteral("Проверка конфигурации %1 превысила 10 секунд")
                         .arg(coreDisplayName(type));
        }
        return false;
    }
    if (check.exitCode() != 0) {
        QString details = QString::fromUtf8(check.readAllStandardError()).trimmed();
        if (details.isEmpty()) {
            details = QString::fromUtf8(check.readAllStandardOutput()).trimmed();
        }
        if (error != nullptr) {
            *error = QStringLiteral("%1 отклонил конфигурацию:\n%2")
                         .arg(coreDisplayName(type), details);
        }
        return false;
    }
    return true;
}

bool SystemdCoreManager::unitIsActive() const
{
    QProcess process;
    process.start(QStringLiteral("systemctl"), {
        QStringLiteral("is-active"),
        QStringLiteral("--quiet"),
        m_unitName,
    });
    return process.waitForFinished(2000) && process.exitCode() == 0;
}

} // namespace lighttunnel
