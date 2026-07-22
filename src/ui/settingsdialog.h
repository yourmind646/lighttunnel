#pragma once

#include "core/appsettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace lighttunnel {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);
    [[nodiscard]] AppSettings settings() const;

private slots:
    void chooseCore();
    void selectedCoreTypeChanged(int index);

private:
    AppSettings m_original;
    CoreType m_shownCoreType{CoreType::SingBox};
    QString m_singBoxPath;
    QString m_xrayPath;
    QComboBox *m_coreType{};
    QLineEdit *m_corePath{};
    QComboBox *m_interface{};
    QComboBox *m_stack{};
    QSpinBox *m_mtu{};
    QCheckBox *m_blockQuic{};
    QCheckBox *m_forceIpv4{};
    QCheckBox *m_startMinimized{};
    QCheckBox *m_autoConnect{};
    QCheckBox *m_autostart{};
    QCheckBox *m_autoUpdateCore{};
};

} // namespace lighttunnel
