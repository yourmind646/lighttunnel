#include "ui/settingsdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lighttunnel {
namespace {

void makePopupOpaque(QComboBox *comboBox)
{
    QAbstractItemView *view = comboBox->view();
    QPalette palette = view->palette();
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#121a2d")));
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#121a2d")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e7ecf5")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2d3a5d")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    view->setPalette(palette);
    view->setAutoFillBackground(true);
    view->viewport()->setPalette(palette);
    view->viewport()->setAutoFillBackground(true);
    view->window()->setPalette(palette);
    view->window()->setAutoFillBackground(true);
}

} // namespace

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

    auto *coreRow = new QWidget(this);
    coreRow->setObjectName(QStringLiteral("transparentContainer"));
    auto *coreLayout = new QHBoxLayout(coreRow);
    coreLayout->setContentsMargins(0, 0, 0, 0);
    m_corePath = new QLineEdit(settings.corePath, coreRow);
    auto *browse = new QPushButton(QStringLiteral("Обзор…"), coreRow);
    coreLayout->addWidget(m_corePath, 1);
    coreLayout->addWidget(browse);
    form->addRow(QStringLiteral("sing-box:"), coreRow);

    m_interface = new QComboBox(this);
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
    makePopupOpaque(m_interface);
    form->addRow(QStringLiteral("Интерфейс выхода:"), m_interface);

    m_stack = new QComboBox(this);
    m_stack->addItem(QStringLiteral("System — быстрее"), QStringLiteral("system"));
    m_stack->addItem(QStringLiteral("gVisor — совместимее"), QStringLiteral("gvisor"));
    m_stack->addItem(QStringLiteral("Mixed"), QStringLiteral("mixed"));
    m_stack->setCurrentIndex(qMax(0, m_stack->findData(settings.tunStack)));
    makePopupOpaque(m_stack);
    form->addRow(QStringLiteral("TUN stack:"), m_stack);

    m_mtu = new QSpinBox(this);
    m_mtu->setRange(1280, 9000);
    m_mtu->setValue(settings.mtu);
    m_mtu->setSuffix(QStringLiteral(" bytes"));
    form->addRow(QStringLiteral("MTU:"), m_mtu);
    mainLayout->addLayout(form);

    m_blockQuic = new QCheckBox(QStringLiteral("Блокировать QUIC (UDP/443) и принудительно использовать TCP"), this);
    m_blockQuic->setChecked(settings.blockQuic);
    m_startMinimized = new QCheckBox(QStringLiteral("Запускать свёрнутым в трей"), this);
    m_startMinimized->setChecked(settings.startMinimized);
    m_autoConnect = new QCheckBox(QStringLiteral("Подключаться к последнему профилю автоматически"), this);
    m_autoConnect->setChecked(settings.autoConnect);
    m_autostart = new QCheckBox(QStringLiteral("Запускать LightTunnel при входе в систему"), this);
    m_autostart->setChecked(settings.autostart);
    m_autoUpdateCore = new QCheckBox(
        QStringLiteral("Автоматически проверять и безопасно обновлять sing-box"), this);
    m_autoUpdateCore->setChecked(settings.autoUpdateCore);

    mainLayout->addWidget(m_blockQuic);
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
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

AppSettings SettingsDialog::settings() const
{
    AppSettings value = m_original;
    value.corePath = m_corePath->text().trimmed();
    value.networkInterface = m_interface->currentData().toString();
    value.tunStack = m_stack->currentData().toString();
    value.mtu = m_mtu->value();
    value.blockQuic = m_blockQuic->isChecked();
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
        QStringLiteral("Выберите sing-box"),
        m_corePath->text(),
        QStringLiteral("Исполняемые файлы (*)"));
    if (!selected.isEmpty()) {
        m_corePath->setText(selected);
    }
}

} // namespace lighttunnel
