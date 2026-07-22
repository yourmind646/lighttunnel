#pragma once

#include "core/appsettings.h"
#include "core/coreupdatemanager.h"
#include "core/profilerepository.h"
#include "core/systemdcoremanager.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QSystemTrayIcon;
class QTextEdit;

namespace lighttunnel {

class StatusIndicator;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    void handleStartup(bool forceMinimized);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void toggleConnection();
    void manageProfiles();
    void openSettings();
    void applyState(SystemdCoreManager::State state);
    void appendLog(const QString &line);
    void showFromTray();
    void requestQuit();
    void profileChanged(int index);
    void checkCoreUpdate();

private:
    void buildUi();
    void buildTray();
    void reloadProfiles(const QString &preferredId = {});
    void updateProfileDetails();
    [[nodiscard]] const VlessProfile *selectedProfile() const;
    [[nodiscard]] static QString stateText(SystemdCoreManager::State state);

    ProfileRepository m_repository;
    QVector<VlessProfile> m_profiles;
    AppSettings m_settings;
    SystemdCoreManager m_coreManager;
    CoreUpdateManager m_coreUpdater;

    QComboBox *m_profileCombo{};
    QFrame *m_statusCard{};
    StatusIndicator *m_statusIndicator{};
    QLabel *m_statusTitle{};
    QLabel *m_statusSubtitle{};
    QLabel *m_statusBadge{};
    QLabel *m_endpointValue{};
    QLabel *m_interfaceValue{};
    QLabel *m_coreValue{};
    QLabel *m_coreUpdateValue{};
    QPushButton *m_checkCoreUpdateButton{};
    QPushButton *m_connectButton{};
    QTextEdit *m_logView{};
    QSystemTrayIcon *m_trayIcon{};
    QAction *m_trayConnectAction{};
    bool m_forceQuit{false};
    bool m_quitPending{false};
};

} // namespace lighttunnel
