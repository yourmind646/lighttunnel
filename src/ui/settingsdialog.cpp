#include "ui/settingsdialog.h"

#include "ui/opaquecombobox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lighttunnel {

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_original(settings)
{
    setWindowTitle(QStringLiteral("Настройки LightTunnel"));
    setMinimumWidth(600);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(12);

    m_coreType = new OpaqueComboBox(this);
    m_coreType->addItem(QStringLiteral("sing-box"), QStringLiteral("sing-box"));
    m_coreType->addItem(QStringLiteral("Xray"), QStringLiteral("xray"));
    m_coreType->setCurrentIndex(m_coreType->findData(coreTypeKey(settings.coreType)));
    form->addRow(QStringLiteral("Ядро:"), m_coreType);

    m_shownCoreType = settings.coreType;
    m_singBoxPath = settings.singBoxPath;
    m_xrayPath = settings.xrayPath;

    auto *coreRow = new QWidget(this);
    coreRow->setObjectName(QStringLiteral("transparentContainer"));
    auto *coreLayout = new QHBoxLayout(coreRow);
    coreLayout->setContentsMargins(0, 0, 0, 0);
    m_corePath = new QLineEdit(settings.corePath(), coreRow);
    auto *browse = new QPushButton(QStringLiteral("Обзор…"), coreRow);
    coreLayout->addWidget(m_corePath, 1);
    coreLayout->addWidget(browse);
    form->addRow(QStringLiteral("Исполняемый файл:"), coreRow);

    m_interface = new OpaqueComboBox(this);
    m_interface->setEditable(false);
    m_interface->addItem(QStringLiteral("Автоматически"), QString());
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        if (networkInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        m_interface->addItem(networkInterface.humanReadableName(), networkInterface.name());
    }
    const int interfaceIndex = m_interface->findData(settings.networkInterface);
    if (interfaceIndex >= 0) {
        m_interface->setCurrentIndex(interfaceIndex);
    } else if (!settings.networkInterface.isEmpty()) {
        m_interface->addItem(settings.networkInterface, settings.networkInterface);
        m_interface->setCurrentIndex(m_interface->count() - 1);
    }
    form->addRow(QStringLiteral("Интерфейс выхода:"), m_interface);

    m_stack = new OpaqueComboBox(this);
    m_stack->addItem(QStringLiteral("System — быстрее"), QStringLiteral("system"));
    m_stack->addItem(QStringLiteral("gVisor — совместимее"), QStringLiteral("gvisor"));
    m_stack->addItem(QStringLiteral("Mixed"), QStringLiteral("mixed"));
    m_stack->setCurrentIndex(qMax(0, m_stack->findData(settings.tunStack)));
    form->addRow(QStringLiteral("TUN stack:"), m_stack);

    m_mtu = new QSpinBox(this);
    m_mtu->setRange(1280, 9000);
    m_mtu->setValue(settings.mtu);
    m_mtu->setSuffix(QStringLiteral(" bytes"));
    form->addRow(QStringLiteral("MTU:"), m_mtu);
    mainLayout->addLayout(form);

    m_blockQuic = new QCheckBox(QStringLiteral("Блокировать QUIC (UDP/443) и принудительно использовать TCP"), this);
    m_blockQuic->setChecked(settings.blockQuic);
    m_forceIpv4 = new QCheckBox(
        QStringLiteral("Принудительно использовать только IPv4 (TCP/UDP)"), this);
    m_forceIpv4->setChecked(settings.forceIpv4);
    m_forceIpv4->setToolTip(QStringLiteral(
        "DNS возвращает только IPv4, а IPv6 захватывается TUN и блокируется без утечки"));
    m_startMinimized = new QCheckBox(QStringLiteral("Запускать свёрнутым в трей"), this);
    m_startMinimized->setChecked(settings.startMinimized);
    m_autoConnect = new QCheckBox(QStringLiteral("Подключаться к последнему профилю автоматически"), this);
    m_autoConnect->setChecked(settings.autoConnect);
    m_autostart = new QCheckBox(QStringLiteral("Запускать LightTunnel при входе в систему"), this);
    m_autostart->setChecked(settings.autostart);
    m_autoUpdateCore = new QCheckBox(
        QStringLiteral("Автоматически проверять и безопасно обновлять выбранное ядро"), this);
    m_autoUpdateCore->setChecked(settings.autoUpdateCore);

    mainLayout->addWidget(m_blockQuic);
    mainLayout->addWidget(m_forceIpv4);
    mainLayout->addWidget(m_startMinimized);
    mainLayout->addWidget(m_autoConnect);
    mainLayout->addWidget(m_autostart);
    mainLayout->addWidget(m_autoUpdateCore);

    auto *hint = new QLabel(QStringLiteral(
        "Для запуска TUN система покажет стандартный диалог Polkit. "
        "GUI продолжает работать от обычного пользователя."), this);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("settingsHint"));
    mainLayout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    mainLayout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &SettingsDialog::chooseCore);
    connect(m_coreType, &QComboBox::currentIndexChanged,
            this, &SettingsDialog::selectedCoreTypeChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    selectedCoreTypeChanged(m_coreType->currentIndex());
}

AppSettings SettingsDialog::settings() const
{
    AppSettings value = m_original;
    value.coreType = coreTypeFromKey(m_coreType->currentData().toString());
    value.singBoxPath = m_singBoxPath;
    value.xrayPath = m_xrayPath;
    value.setCorePath(m_corePath->text());
    value.networkInterface = m_interface->currentData().toString();
    value.tunStack = m_stack->currentData().toString();
    value.mtu = m_mtu->value();
    value.blockQuic = m_blockQuic->isChecked();
    value.forceIpv4 = m_forceIpv4->isChecked();
    value.startMinimized = m_startMinimized->isChecked();
    value.autoConnect = m_autoConnect->isChecked();
    value.autostart = m_autostart->isChecked();
    value.autoUpdateCore = m_autoUpdateCore->isChecked();
    return value;
}

void SettingsDialog::chooseCore()
{
    const QString selected = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Выберите %1").arg(coreDisplayName(m_shownCoreType)),
        m_corePath->text(),
        QStringLiteral("Исполняемые файлы (*)"));
    if (!selected.isEmpty()) {
        m_corePath->setText(selected);
    }
}

void SettingsDialog::selectedCoreTypeChanged(int index)
{
    Q_UNUSED(index)
    if (m_shownCoreType == CoreType::Xray) {
        m_xrayPath = m_corePath->text().trimmed();
    } else {
        m_singBoxPath = m_corePath->text().trimmed();
    }

    m_shownCoreType = coreTypeFromKey(m_coreType->currentData().toString());
    m_corePath->setText(m_shownCoreType == CoreType::Xray ? m_xrayPath : m_singBoxPath);
    m_stack->setEnabled(m_shownCoreType == CoreType::SingBox);
    m_stack->setToolTip(m_shownCoreType == CoreType::Xray
                            ? QStringLiteral("Xray использует собственный нативный TUN")
                            : QString());
}

} // namespace lighttunnel
