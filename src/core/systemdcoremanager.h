#pragma once

#include "core/appsettings.h"
#include "core/vlessprofile.h"

#include <QObject>
#include <QProcess>
#include <QTimer>

namespace lighttunnel {

class SystemdCoreManager final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Starting,
        Connected,
        Stopping,
        Error,
    };
    Q_ENUM(State)

    explicit SystemdCoreManager(QObject *parent = nullptr);
    ~SystemdCoreManager() override;

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] QString unitName() const { return m_unitName; }
    [[nodiscard]] QString configPath() const { return m_configPath; }

public slots:
    void connectTunnel(const VlessProfile &profile, const AppSettings &settings);
    void disconnectTunnel();
    void refreshState();

signals:
    void stateChanged(lighttunnel::SystemdCoreManager::State state);
    void logLine(const QString &line);
    void errorOccurred(const QString &message);

private:
    void setState(State state);
    void beginJournalFollow();
    void stopJournalFollow();
    void runPrivileged(const QStringList &arguments, State transitionalState);
    [[nodiscard]] bool validateConfig(const QString &corePath, QString *error);
    [[nodiscard]] bool unitIsActive() const;

    State m_state{State::Disconnected};
    QString m_unitName;
    QString m_configPath;
    QString m_corePath;
    QProcess m_controlProcess;
    QProcess m_journalProcess;
    QTimer m_stateTimer;
    bool m_controlIsStart{false};
};

} // namespace lighttunnel
