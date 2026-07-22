#include "ui/mainwindow.h"

#include "core/autostartmanager.h"
#include "ui/opaquecombobox.h"
#include "ui/profilemanagerdialog.h"
#include "ui/settingsdialog.h"
#include "ui/statusindicator.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace lighttunnel {
namespace {

QFrame *makeCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

QLabel *makeCaption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("caption"));
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(AppSettings::load())
{
    setWindowTitle(QStringLiteral("LightTunnel"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/lighttunnel.svg")));
    setMinimumSize(760, 600);
    resize(880, 680);

    buildUi();
    buildTray();
    reloadProfiles(m_settings.lastProfileId);
    m_coreUpdater.setCore(m_settings.coreType, m_settings.corePath());

    if (m_settings.autostart) {
        QString autostartError;
        if (!AutostartManager::setEnabled(true, &autostartError)) {
            appendLog(autostartError);
        }
    }

    connect(&m_coreManager, &SystemdCoreManager::stateChanged, this, &MainWindow::applyState);
    connect(&m_coreManager, &SystemdCoreManager::logLine, this, &MainWindow::appendLog);
    connect(&m_coreManager, &SystemdCoreManager::errorOccurred, this, [this](const QString &message) {
        QMessageBox::critical(this, QStringLiteral("LightTunnel"), message);
    });
    connect(&m_coreUpdater, &CoreUpdateManager::statusChanged, this, [this](const QString &status) {
        m_coreUpdateValue->setText(status);
        m_coreUpdateValue->setToolTip(status);
    });
    connect(&m_coreUpdater, &CoreUpdateManager::progressChanged, this, [this](int percent) {
        m_coreUpdateValue->setText(QStringLiteral("Скачивание %1 · %2%")
                                       .arg(coreDisplayName(m_coreUpdater.coreType()))
                                       .arg(percent));
    });
    connect(&m_coreUpdater, &CoreUpdateManager::currentVersionChanged, this, [this](const QString &) {
        updateProfileDetails();
    });
    connect(&m_coreUpdater, &CoreUpdateManager::coreInstalled, this,
            [this](const QString &path, const QString &version) {
                const CoreType installedType = m_coreUpdater.coreType();
                if (installedType == CoreType::Xray) {
                    m_settings.xrayPath = path;
                } else {
                    m_settings.singBoxPath = path;
                }
                m_settings.save();
                updateProfileDetails();
                const bool connected = m_coreManager.state() == SystemdCoreManager::State::Connected;
                appendLog(connected
                              ? QStringLiteral("%1 %2 установлен и будет использован после переподключения")
                                    .arg(coreDisplayName(installedType), version)
                              : QStringLiteral("%1 %2 установлен и готов к работе")
                                    .arg(coreDisplayName(installedType), version));
            });
    connect(&m_coreUpdater, &CoreUpdateManager::errorOccurred, this, [this](const QString &message) {
        m_coreUpdateValue->setText(QStringLiteral("Не удалось проверить обновление"));
        m_coreUpdateValue->setToolTip(message);
        appendLog(message);
    });
    applyState(m_coreManager.state());

    QTimer::singleShot(1600, this, [this] {
        if (m_settings.autoUpdateCore || m_settings.corePath().isEmpty()) {
            m_coreUpdater.checkForUpdates(false);
        }
    });
}

void MainWindow::handleStartup()
{
    if (!m_settings.startMinimized || !QSystemTrayIcon::isSystemTrayAvailable()) {
        show();
        raise();
        activateWindow();
    }

    if (m_settings.autoConnect && selectedProfile() != nullptr
        && m_coreManager.state() == SystemdCoreManager::State::Disconnected) {
        QTimer::singleShot(700, this, &MainWindow::toggleConnection);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_forceQuit && QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
        m_trayIcon->showMessage(QStringLiteral("LightTunnel"),
                                QStringLiteral("Приложение продолжает работать в трее."),
                                QSystemTrayIcon::Information,
                                2500);
        return;
    }
    event->accept();
}

void MainWindow::toggleConnection()
{
    switch (m_coreManager.state()) {
    case SystemdCoreManager::State::Connected:
        m_coreManager.disconnectTunnel();
        break;
    case SystemdCoreManager::State::Disconnected:
    case SystemdCoreManager::State::Error: {
        const VlessProfile *profile = selectedProfile();
        if (profile == nullptr) {
            QMessageBox::information(this,
                                     QStringLiteral("Нет профиля"),
                                     QStringLiteral("Сначала импортируйте VLESS-профиль."));
            manageProfiles();
            return;
        }
        m_coreManager.connectTunnel(*profile, m_settings);
        break;
    }
    case SystemdCoreManager::State::Starting:
    case SystemdCoreManager::State::Stopping:
        break;
    }
}

void MainWindow::manageProfiles()
{
    ProfileManagerDialog dialog(m_profiles, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString selectedId = selectedProfile() == nullptr ? QString() : selectedProfile()->id;
    QString error;
    if (!m_repository.save(dialog.profiles(), &error)) {
        QMessageBox::critical(this, QStringLiteral("Ошибка сохранения"), error);
        return;
    }
    reloadProfiles(selectedId);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    AppSettings newSettings = dialog.settings();
    if (m_coreUpdater.isBusy()
        && (newSettings.coreType != m_settings.coreType
            || newSettings.corePath() != m_settings.corePath())) {
        QMessageBox::information(
            this,
            QStringLiteral("Обновление ядра"),
            QStringLiteral("Дождитесь завершения текущего обновления перед сменой ядра или его пути."));
        newSettings.coreType = m_settings.coreType;
        newSettings.singBoxPath = m_settings.singBoxPath;
        newSettings.xrayPath = m_settings.xrayPath;
    }
    QString error;
    if (!AutostartManager::setEnabled(newSettings.autostart, &error)) {
        QMessageBox::warning(this, QStringLiteral("Автозапуск"), error);
    }
    m_settings = newSettings;
    m_settings.save();
    m_coreUpdater.setCore(m_settings.coreType, m_settings.corePath());
    updateProfileDetails();
    appendLog(QStringLiteral("Настройки сохранены"));
    if (m_settings.autoUpdateCore || m_settings.corePath().isEmpty()) {
        m_coreUpdater.checkForUpdates(false);
    }
}

void MainWindow::applyState(SystemdCoreManager::State state)
{
    const QString text = stateText(state);
    m_statusTitle->setText(text);

    QString stateProperty = QStringLiteral("disconnected");
    StatusIndicator::Mode indicatorMode = StatusIndicator::Mode::Disconnected;
    QString badgeText = QStringLiteral("ГОТОВ К ЗАПУСКУ");
    if (state == SystemdCoreManager::State::Connected) {
        stateProperty = QStringLiteral("connected");
        indicatorMode = StatusIndicator::Mode::Connected;
        badgeText = QStringLiteral("TUN АКТИВЕН");
    } else if (state == SystemdCoreManager::State::Starting
               || state == SystemdCoreManager::State::Stopping) {
        stateProperty = QStringLiteral("busy");
        indicatorMode = StatusIndicator::Mode::Busy;
        badgeText = state == SystemdCoreManager::State::Starting
            ? QStringLiteral("ПОДКЛЮЧЕНИЕ")
            : QStringLiteral("ОТКЛЮЧЕНИЕ");
    } else if (state == SystemdCoreManager::State::Error) {
        stateProperty = QStringLiteral("error");
        indicatorMode = StatusIndicator::Mode::Error;
        badgeText = QStringLiteral("ТРЕБУЕТ ВНИМАНИЯ");
    }
    m_statusIndicator->setMode(indicatorMode);
    m_statusBadge->setText(badgeText);
    m_statusBadge->setProperty("connectionState", stateProperty);
    m_statusCard->setProperty("connectionState", stateProperty);
    for (QWidget *widget : {static_cast<QWidget *>(m_statusBadge), static_cast<QWidget *>(m_statusCard)}) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }

    const bool busy = state == SystemdCoreManager::State::Starting
        || state == SystemdCoreManager::State::Stopping;
    m_profileCombo->setEnabled(!busy && state != SystemdCoreManager::State::Connected);
    m_connectButton->setEnabled(!busy && (!m_profiles.isEmpty() || state == SystemdCoreManager::State::Connected));

    if (state == SystemdCoreManager::State::Connected) {
        m_connectButton->setText(QStringLiteral("Отключиться"));
        m_connectButton->setProperty("connected", true);
        m_statusSubtitle->setText(QStringLiteral("Весь системный трафик проходит через защищённый TUN"));
        m_trayConnectAction->setText(QStringLiteral("Отключиться"));
        m_trayIcon->setToolTip(QStringLiteral("LightTunnel — подключено"));
    } else if (busy) {
        m_connectButton->setText(state == SystemdCoreManager::State::Starting
                                     ? QStringLiteral("Подключение…")
                                     : QStringLiteral("Отключение…"));
        m_statusSubtitle->setText(QStringLiteral("Ожидание системной операции"));
        m_trayConnectAction->setText(m_connectButton->text());
    } else {
        m_connectButton->setText(QStringLiteral("Подключиться"));
        m_connectButton->setProperty("connected", false);
        m_statusSubtitle->setText(QStringLiteral("Выберите профиль и запустите защищённое соединение"));
        m_trayConnectAction->setText(QStringLiteral("Подключиться"));
        m_trayIcon->setToolTip(QStringLiteral("LightTunnel — отключено"));
    }
    m_connectButton->style()->unpolish(m_connectButton);
    m_connectButton->style()->polish(m_connectButton);

    if (m_quitPending && state == SystemdCoreManager::State::Disconnected) {
        m_forceQuit = true;
        qApp->quit();
    }
}

void MainWindow::appendLog(const QString &line)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->append(QStringLiteral("<span style='color:#7d8799'>%1</span> %2")
                          .arg(timestamp, line.toHtmlEscaped()));
}

void MainWindow::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::requestQuit()
{
    if (m_coreManager.state() == SystemdCoreManager::State::Connected) {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("Завершить LightTunnel?"),
            QStringLiteral("Активный VPN будет отключён."));
        if (answer != QMessageBox::Yes) {
            return;
        }
        m_quitPending = true;
        m_coreManager.disconnectTunnel();
        return;
    }
    m_forceQuit = true;
    qApp->quit();
}

void MainWindow::profileChanged(int index)
{
    Q_UNUSED(index)
    if (const VlessProfile *profile = selectedProfile(); profile != nullptr) {
        m_settings.lastProfileId = profile->id;
        m_settings.save();
    }
    updateProfileDetails();
}

void MainWindow::checkCoreUpdate()
{
    m_coreUpdater.setCore(m_settings.coreType, m_settings.corePath());
    m_coreUpdater.checkForUpdates(true);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(28, 24, 28, 28);
    mainLayout->setSpacing(18);

    auto *header = new QHBoxLayout;
    auto *brandIcon = new QLabel(central);
    brandIcon->setPixmap(QIcon(QStringLiteral(":/icons/lighttunnel.svg")).pixmap(42, 42));
    auto *brandText = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("LightTunnel"), central);
    title->setObjectName(QStringLiteral("brandTitle"));
    auto *tagline = new QLabel(QStringLiteral("Лёгкий VLESS-клиент для Linux"), central);
    tagline->setObjectName(QStringLiteral("caption"));
    brandText->addWidget(title);
    brandText->addWidget(tagline);
    header->addWidget(brandIcon);
    header->addLayout(brandText);
    header->addStretch();
    auto *settingsButton = new QPushButton(QStringLiteral("Настройки"), central);
    settingsButton->setObjectName(QStringLiteral("secondaryButton"));
    header->addWidget(settingsButton);
    mainLayout->addLayout(header);

    m_statusCard = makeCard(central);
    m_statusCard->setObjectName(QStringLiteral("connectionCard"));
    auto *statusLayout = new QVBoxLayout(m_statusCard);
    statusLayout->setContentsMargins(24, 22, 24, 24);
    statusLayout->setSpacing(16);

    auto *statusRow = new QHBoxLayout;
    m_statusIndicator = new StatusIndicator(m_statusCard);
    auto *statusTexts = new QVBoxLayout;
    m_statusTitle = new QLabel(m_statusCard);
    m_statusTitle->setObjectName(QStringLiteral("statusTitle"));
    m_statusSubtitle = new QLabel(m_statusCard);
    m_statusSubtitle->setObjectName(QStringLiteral("caption"));
    m_statusSubtitle->setWordWrap(true);
    statusTexts->addWidget(m_statusTitle);
    statusTexts->addWidget(m_statusSubtitle);
    statusRow->addWidget(m_statusIndicator, 0, Qt::AlignVCenter);
    statusRow->addLayout(statusTexts);
    statusRow->addStretch();
    m_statusBadge = new QLabel(m_statusCard);
    m_statusBadge->setObjectName(QStringLiteral("statusBadge"));
    m_statusBadge->setAlignment(Qt::AlignCenter);
    statusRow->addWidget(m_statusBadge, 0, Qt::AlignTop);
    statusLayout->addLayout(statusRow);

    auto *profileRow = new QHBoxLayout;
    m_profileCombo = new OpaqueComboBox(m_statusCard);
    m_profileCombo->setMinimumHeight(42);
    auto *manageButton = new QPushButton(QStringLiteral("Профили…"), m_statusCard);
    manageButton->setObjectName(QStringLiteral("secondaryButton"));
    manageButton->setMinimumHeight(42);
    profileRow->addWidget(m_profileCombo, 1);
    profileRow->addWidget(manageButton);
    statusLayout->addLayout(profileRow);

    m_connectButton = new QPushButton(QStringLiteral("Подключиться"), m_statusCard);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    m_connectButton->setMinimumHeight(48);
    statusLayout->addWidget(m_connectButton);
    mainLayout->addWidget(m_statusCard);

    auto *tabs = new QTabWidget(central);
    tabs->setDocumentMode(true);
    auto *overview = new QWidget(tabs);
    auto *overviewLayout = new QVBoxLayout(overview);
    overviewLayout->setContentsMargins(0, 16, 0, 0);
    auto *detailsCard = makeCard(overview);
    auto *details = new QGridLayout(detailsCard);
    details->setContentsMargins(22, 18, 22, 18);
    details->setHorizontalSpacing(28);
    details->setVerticalSpacing(14);
    details->addWidget(makeCaption(QStringLiteral("Сервер"), detailsCard), 0, 0);
    details->addWidget(makeCaption(QStringLiteral("Интерфейс"), detailsCard), 1, 0);
    details->addWidget(makeCaption(QStringLiteral("Ядро"), detailsCard), 2, 0);
    m_endpointValue = new QLabel(QStringLiteral("—"), detailsCard);
    m_interfaceValue = new QLabel(QStringLiteral("—"), detailsCard);
    m_coreValue = new QLabel(QStringLiteral("—"), detailsCard);
    m_coreUpdateValue = new QLabel(QStringLiteral("Не проверялось"), detailsCard);
    m_endpointValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_coreValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->addWidget(m_endpointValue, 0, 1);
    details->addWidget(m_interfaceValue, 1, 1);
    details->addWidget(m_coreValue, 2, 1);
    details->addWidget(makeCaption(QStringLiteral("Обновление"), detailsCard), 3, 0);
    auto *coreUpdateRow = new QWidget(detailsCard);
    coreUpdateRow->setObjectName(QStringLiteral("transparentContainer"));
    auto *coreUpdateLayout = new QHBoxLayout(coreUpdateRow);
    coreUpdateLayout->setContentsMargins(0, 0, 0, 0);
    coreUpdateLayout->setSpacing(10);
    coreUpdateLayout->addWidget(m_coreUpdateValue, 1);
    m_checkCoreUpdateButton = new QPushButton(QStringLiteral("Проверить"), coreUpdateRow);
    m_checkCoreUpdateButton->setObjectName(QStringLiteral("secondaryButton"));
    coreUpdateLayout->addWidget(m_checkCoreUpdateButton);
    details->addWidget(coreUpdateRow, 3, 1);
    details->setColumnStretch(1, 1);
    overviewLayout->addWidget(detailsCard);
    overviewLayout->addStretch();

    auto *logs = new QWidget(tabs);
    auto *logsLayout = new QVBoxLayout(logs);
    logsLayout->setContentsMargins(0, 16, 0, 0);
    m_logView = new QTextEdit(logs);
    m_logView->setReadOnly(true);
    m_logView->setObjectName(QStringLiteral("logView"));
    m_logView->setPlaceholderText(QStringLiteral("Здесь появятся сообщения VPN-ядра…"));
    logsLayout->addWidget(m_logView);

    tabs->addTab(overview, QStringLiteral("Обзор"));
    tabs->addTab(logs, QStringLiteral("Журнал"));
    mainLayout->addWidget(tabs, 1);
    setCentralWidget(central);

    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(manageButton, &QPushButton::clicked, this, &MainWindow::manageProfiles);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(m_checkCoreUpdateButton, &QPushButton::clicked, this, &MainWindow::checkCoreUpdate);
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::profileChanged);
}

void MainWindow::buildTray()
{
    m_trayIcon = new QSystemTrayIcon(
        QIcon(QStringLiteral(":/icons/lighttunnel.svg")), this);
    auto *menu = new QMenu(this);
    auto *showAction = menu->addAction(QStringLiteral("Открыть LightTunnel"));
    m_trayConnectAction = menu->addAction(QStringLiteral("Подключиться"));
    menu->addSeparator();
    auto *quitAction = menu->addAction(QStringLiteral("Выход"));
    m_trayIcon->setContextMenu(menu);
    m_trayIcon->show();

    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);
    connect(m_trayConnectAction, &QAction::triggered, this, &MainWindow::toggleConnection);
    connect(quitAction, &QAction::triggered, this, &MainWindow::requestQuit);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showFromTray();
        }
    });
}

void MainWindow::reloadProfiles(const QString &preferredId)
{
    QString error;
    m_profiles = m_repository.load(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Профили"), error);
    }

    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    int selectedIndex = 0;
    for (qsizetype index = 0; index < m_profiles.size(); ++index) {
        const VlessProfile &profile = m_profiles.at(index);
        m_profileCombo->addItem(profile.name, profile.id);
        if (!preferredId.isEmpty() && profile.id == preferredId) {
            selectedIndex = static_cast<int>(index);
        }
    }
    if (!m_profiles.isEmpty()) {
        m_profileCombo->setCurrentIndex(selectedIndex);
    }
    m_profileCombo->blockSignals(false);
    updateProfileDetails();
    applyState(m_coreManager.state());
}

void MainWindow::updateProfileDetails()
{
    const VlessProfile *profile = selectedProfile();
    m_endpointValue->setText(profile == nullptr ? QStringLiteral("—") : profile->endpoint());
    const QString interfaceName = m_settings.effectiveInterface();
    m_interfaceValue->setText(interfaceName.isEmpty() ? QStringLiteral("Не определён") : interfaceName);
    QString version;
    if (m_coreUpdater.coreType() == m_settings.coreType
        && m_coreUpdater.currentPath() == m_settings.corePath()) {
        version = m_coreUpdater.currentVersion();
    } else {
        version = CoreUpdateManager::detectVersion(m_settings.corePath(), m_settings.coreType);
    }
    if (m_settings.corePath().isEmpty()) {
        m_coreValue->setText(QStringLiteral("Не установлен"));
        if (!m_coreUpdater.isBusy()) {
            m_coreUpdateValue->setText(QStringLiteral("Будет загружен с GitHub Releases"));
        }
    } else if (!version.isEmpty()) {
        m_coreValue->setText(QStringLiteral("%1 %2")
                                 .arg(coreDisplayName(m_settings.coreType), version));
    } else {
        m_coreValue->setText(QStringLiteral("%1 · версия неизвестна")
                                 .arg(coreDisplayName(m_settings.coreType)));
    }
    m_coreValue->setToolTip(m_settings.corePath());
}

const VlessProfile *MainWindow::selectedProfile() const
{
    const int index = m_profileCombo->currentIndex();
    if (index < 0 || index >= m_profiles.size()) {
        return nullptr;
    }
    return &m_profiles.at(index);
}

QString MainWindow::stateText(SystemdCoreManager::State state)
{
    switch (state) {
    case SystemdCoreManager::State::Disconnected:
        return QStringLiteral("VPN отключён");
    case SystemdCoreManager::State::Starting:
        return QStringLiteral("Создаём защищённый туннель");
    case SystemdCoreManager::State::Connected:
        return QStringLiteral("Соединение защищено");
    case SystemdCoreManager::State::Stopping:
        return QStringLiteral("Отключение");
    case SystemdCoreManager::State::Error:
        return QStringLiteral("Ошибка");
    }
    return QStringLiteral("Неизвестно");
}

} // namespace lighttunnel
