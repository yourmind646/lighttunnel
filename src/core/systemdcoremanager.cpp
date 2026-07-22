#include "core/systemdcoremanager.h"

#include "core/singboxconfigbuilder.h"
#include "core/systemdcommandbuilder.h"
#include "core/xrayconfigbuilder.h"

#include <QDir>
#include <QFileInfo>

#include <unistd.h>

namespace lighttunnel {

SystemdCoreManager::SystemdCoreManager(QObject *parent)
    : QObject(parent)
    , m_unitName(QStringLiteral("lighttunnel-core-%1.service").arg(static_cast<qulonglong>(::getuid())))
    , m_configPath(QDir(AppSettings::runtimeDirectory()).filePath(QStringLiteral("config.json")))
{
    m_stateTimer.setInterval(1500);
    connect(&m_stateTimer, &QTimer::timeout, this, &SystemdCoreManager::refreshState);

    connect(&m_controlProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                const QString standardError = QString::fromUtf8(m_controlProcess.readAllStandardError()).trimmed();
                if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                    setState(State::Error);
                    const QString message = standardError.isEmpty()
                        ? QStringLiteral("Привилегированная операция завершилась с кодом %1").arg(exitCode)
                        : standardError;
                    emit errorOccurred(message);
                    emit logLine(QStringLiteral("Ошибка: %1").arg(message));
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

    emit logLine(QStringLiteral("Конфигурация проверена. Запрашиваю права на создание TUN…"));
    const QStringList arguments = SystemdCommandBuilder::startArguments(
        m_unitName,
        m_corePath,
        m_configPath,
        static_cast<quint32>(::getuid()),
        static_cast<quint32>(::getgid()),
        m_runningCoreType);
    m_controlIsStart = true;
    runPrivileged(arguments, State::Starting);
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
    runPrivileged({QStringLiteral("systemctl"), QStringLiteral("stop"), m_unitName}, State::Stopping);
}

void SystemdCoreManager::refreshState()
{
    if (m_controlProcess.state() != QProcess::NotRunning) {
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

void SystemdCoreManager::runPrivileged(const QStringList &arguments, State transitionalState)
{
    if (m_controlProcess.state() != QProcess::NotRunning) {
        emit errorOccurred(QStringLiteral("Другая системная операция ещё выполняется"));
        return;
    }
    setState(transitionalState);
    m_controlProcess.start(QStringLiteral("pkexec"), arguments);
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
