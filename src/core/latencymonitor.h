#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QTcpSocket;

namespace lighttunnel {

class LatencyMonitor final : public QObject {
    Q_OBJECT

public:
    explicit LatencyMonitor(QObject *parent = nullptr);

    void start(const QString &host, quint16 port);
    void stop();

signals:
    // A negative value means that no current measurement is available.
    void latencyChanged(int milliseconds);

private slots:
    void probe();
    void probeSucceeded();
    void probeFailed();

private:
    static constexpr int ProbeIntervalMs = 5000;
    static constexpr int ProbeTimeoutMs = 3000;

    QString m_host;
    quint16 m_port{0};
    QTcpSocket *m_socket{};
    QTimer m_probeTimer;
    QTimer m_timeoutTimer;
    QElapsedTimer m_elapsed;
    bool m_probeInProgress{false};
};

} // namespace lighttunnel
