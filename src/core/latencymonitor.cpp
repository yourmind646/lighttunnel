#include "core/latencymonitor.h"

#include <QAbstractSocket>
#include <QNetworkProxy>
#include <QTcpSocket>

namespace lighttunnel {

LatencyMonitor::LatencyMonitor(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    m_probeTimer.setInterval(ProbeIntervalMs);
    m_timeoutTimer.setSingleShot(true);

    connect(&m_probeTimer, &QTimer::timeout, this, &LatencyMonitor::probe);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &LatencyMonitor::probeFailed);
    connect(m_socket, &QTcpSocket::connected, this, &LatencyMonitor::probeSucceeded);
    connect(m_socket, &QTcpSocket::readyRead, this, &LatencyMonitor::responseReceived);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        probeFailed();
    });
}

void LatencyMonitor::start(const QString &host, quint16 port)
{
    startInternal({}, 0, host, port, {});
}

void LatencyMonitor::startViaSocks(const QString &proxyHost, quint16 proxyPort,
                                   const QString &host, quint16 port)
{
    startInternal(proxyHost, proxyPort, host, port, {});
}

void LatencyMonitor::startHttpViaSocks(const QString &proxyHost, quint16 proxyPort,
                                       const QString &host, quint16 port,
                                       const QString &httpHost)
{
    const QByteArray request = QByteArrayLiteral("HEAD / HTTP/1.1\r\nHost: ")
        + httpHost.trimmed().toUtf8()
        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    startInternal(proxyHost, proxyPort, host, port, request);
}

void LatencyMonitor::startInternal(const QString &proxyHost, quint16 proxyPort,
                                   const QString &host, quint16 port,
                                   const QByteArray &probeRequest)
{
    const QString normalizedHost = host.trimmed();
    const QString normalizedProxyHost = proxyHost.trimmed();
    if (normalizedHost.isEmpty() || port == 0
        || (normalizedProxyHost.isEmpty() != (proxyPort == 0))) {
        stop();
        return;
    }

    const bool targetChanged = normalizedHost != m_host || port != m_port
        || normalizedProxyHost != m_proxyHost || proxyPort != m_proxyPort
        || probeRequest != m_probeRequest;
    m_proxyHost = normalizedProxyHost;
    m_proxyPort = proxyPort;
    m_host = normalizedHost;
    m_port = port;
    m_probeRequest = probeRequest;
    if (targetChanged) {
        m_socket->abort();
        m_timeoutTimer.stop();
        m_probeInProgress = false;
        emit latencyChanged(-1);
    }

    if (!m_probeTimer.isActive()) {
        m_probeTimer.start();
    }
    if (!m_probeInProgress) {
        probe();
    }
}

void LatencyMonitor::stop()
{
    m_probeTimer.stop();
    m_timeoutTimer.stop();
    m_socket->abort();
    m_probeInProgress = false;
    m_proxyHost.clear();
    m_proxyPort = 0;
    m_host.clear();
    m_port = 0;
    m_probeRequest.clear();
    emit latencyChanged(-1);
}

void LatencyMonitor::probe()
{
    if (m_probeInProgress || m_host.isEmpty() || m_port == 0) {
        return;
    }

    m_probeInProgress = true;
    m_elapsed.restart();
    m_timeoutTimer.start(ProbeTimeoutMs);
    if (m_proxyHost.isEmpty()) {
        m_socket->setProxy(QNetworkProxy::NoProxy);
    } else {
        m_socket->setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,
                                         m_proxyHost, m_proxyPort));
    }
    m_socket->connectToHost(m_host, m_port);
}

void LatencyMonitor::probeSucceeded()
{
    if (!m_probeInProgress) {
        return;
    }

    if (!m_probeRequest.isEmpty()) {
        if (m_socket->write(m_probeRequest) != m_probeRequest.size()) {
            probeFailed();
        }
        return;
    }

    responseReceived();
}

void LatencyMonitor::responseReceived()
{
    if (!m_probeInProgress
        || (!m_probeRequest.isEmpty() && m_socket->bytesAvailable() == 0)) {
        return;
    }

    const int milliseconds = qMax(1, static_cast<int>(m_elapsed.elapsed()));
    m_probeInProgress = false;
    m_timeoutTimer.stop();
    m_socket->abort();
    emit latencyChanged(milliseconds);
}

void LatencyMonitor::probeFailed()
{
    if (!m_probeInProgress) {
        return;
    }

    m_probeInProgress = false;
    m_timeoutTimer.stop();
    m_socket->abort();
    emit latencyChanged(-1);
}

} // namespace lighttunnel
