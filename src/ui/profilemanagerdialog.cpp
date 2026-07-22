#include "ui/profilemanagerdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace lighttunnel {

ProfileManagerDialog::ProfileManagerDialog(QVector<VlessProfile> profiles, QWidget *parent)
    : QDialog(parent)
    , m_profiles(std::move(profiles))
    , m_network(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Профили VLESS"));
    setMinimumSize(660, 430);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto *intro = new QLabel(QStringLiteral(
        "Импортируйте одну или несколько ссылок <b>vless://</b>. "
        "Секретные параметры сохраняются локально с правами 0600."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list, 1);

    m_details = new QLabel(this);
    m_details->setObjectName(QStringLiteral("profileDetails"));
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_details);

    auto *actions = new QHBoxLayout;
    auto *importButton = new QPushButton(QStringLiteral("Импортировать…"), this);
    auto *subscriptionButton = new QPushButton(QStringLiteral("Из подписки URL…"), this);
    m_renameButton = new QPushButton(QStringLiteral("Переименовать"), this);
    m_removeButton = new QPushButton(QStringLiteral("Удалить"), this);
    actions->addWidget(importButton);
    actions->addWidget(subscriptionButton);
    actions->addWidget(m_renameButton);
    actions->addWidget(m_removeButton);
    actions->addStretch();
    layout->addLayout(actions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Сохранить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
    layout->addWidget(buttons);

    connect(importButton, &QPushButton::clicked, this, &ProfileManagerDialog::importProfiles);
    connect(subscriptionButton, &QPushButton::clicked, this, &ProfileManagerDialog::importSubscription);
    connect(m_renameButton, &QPushButton::clicked, this, &ProfileManagerDialog::renameProfile);
    connect(m_removeButton, &QPushButton::clicked, this, &ProfileManagerDialog::removeProfile);
    connect(m_list, &QListWidget::currentRowChanged, this, &ProfileManagerDialog::updateSelection);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ProfileManagerDialog::renameProfile);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildList(m_profiles.isEmpty() ? -1 : 0);
}

void ProfileManagerDialog::importProfiles()
{
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(
        this,
        QStringLiteral("Импорт VLESS"),
        QStringLiteral("Вставьте ссылки vless://, по одной на строку:"),
        {},
        &accepted);
    if (!accepted || text.trimmed().isEmpty()) {
        return;
    }

    importPayload(text.toUtf8(), QStringLiteral("вставленного текста"));
}

void ProfileManagerDialog::importSubscription()
{
    bool accepted = false;
    const QString value = QInputDialog::getText(
        this,
        QStringLiteral("Импорт подписки"),
        QStringLiteral("HTTPS URL подписки:"),
        QLineEdit::Normal,
        {},
        &accepted)
                              .trimmed();
    if (!accepted || value.isEmpty()) {
        return;
    }

    const QUrl url = QUrl::fromUserInput(value);
    if (!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
        QMessageBox::warning(this,
                             QStringLiteral("Некорректный URL"),
                             QStringLiteral("Поддерживаются только адреса HTTPS и HTTP."));
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LightTunnel/0.1"));
    QNetworkReply *reply = m_network->get(request);

    auto *progress = new QProgressDialog(QStringLiteral("Загрузка подписки…"),
                                         QStringLiteral("Отмена"),
                                         0,
                                         0,
                                         this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->show();
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, url] {
        progress->close();
        progress->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this,
                                 QStringLiteral("Ошибка подписки"),
                                 QStringLiteral("Не удалось загрузить %1:\n%2")
                                     .arg(url.host(), reply->errorString()));
            reply->deleteLater();
            return;
        }
        const QByteArray payload = reply->read(2 * 1024 * 1024 + 1);
        reply->deleteLater();
        if (payload.size() > 2 * 1024 * 1024) {
            QMessageBox::warning(this,
                                 QStringLiteral("Подписка слишком большая"),
                                 QStringLiteral("Максимальный размер ответа — 2 МиБ."));
            return;
        }
        importPayload(payload, url.host());
    });
}

void ProfileManagerDialog::importPayload(const QByteArray &payload, const QString &sourceName)
{
    QByteArray normalized = payload.trimmed();
    if (!normalized.contains("vless://")) {
        const QByteArray decoded = QByteArray::fromBase64(normalized);
        if (decoded.contains("vless://")) {
            normalized = decoded;
        }
    }

    int imported = 0;
    int updated = 0;
    QStringList errors;
    const QString text = QString::fromUtf8(normalized);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString error;
        const auto profile = VlessProfile::fromUri(line.trimmed(), &error);
        if (!profile.has_value()) {
            if (line.trimmed().startsWith(QStringLiteral("vless://"))) {
                errors.append(error);
            }
            continue;
        }
        const auto existing = std::find_if(m_profiles.begin(), m_profiles.end(), [&profile](const VlessProfile &candidate) {
            return candidate.uuid == profile->uuid && candidate.server == profile->server
                && candidate.serverPort == profile->serverPort;
        });
        if (existing == m_profiles.end()) {
            m_profiles.push_back(*profile);
            ++imported;
        } else {
            VlessProfile replacement = *profile;
            replacement.id = existing->id;
            *existing = std::move(replacement);
            ++updated;
        }
    }

    rebuildList(m_profiles.isEmpty() ? -1 : m_profiles.size() - 1);
    if (imported == 0 && updated == 0) {
        QMessageBox::warning(this,
                             QStringLiteral("Профили не найдены"),
                             QStringLiteral("В данных от %1 нет поддерживаемых ссылок vless://.").arg(sourceName));
        return;
    }
    if (!errors.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Часть ссылок не импортирована"),
                             QStringLiteral("Добавлено: %1, обновлено: %2\n\n%3")
                                 .arg(imported)
                                 .arg(updated)
                                 .arg(errors.join(QLatin1Char('\n'))));
    }
}

void ProfileManagerDialog::renameProfile()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_profiles.size()) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this,
                                                QStringLiteral("Название профиля"),
                                                QStringLiteral("Название:"),
                                                QLineEdit::Normal,
                                                m_profiles.at(row).name,
                                                &accepted)
                             .trimmed();
    if (accepted && !name.isEmpty()) {
        m_profiles[row].name = name;
        rebuildList(row);
    }
}

void ProfileManagerDialog::removeProfile()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_profiles.size()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Удалить профиль?"),
        QStringLiteral("Профиль «%1» будет удалён локально.").arg(m_profiles.at(row).name));
    if (answer == QMessageBox::Yes) {
        m_profiles.removeAt(row);
        rebuildList(qMin(row, m_profiles.size() - 1));
    }
}

void ProfileManagerDialog::updateSelection()
{
    const int row = m_list->currentRow();
    const bool valid = row >= 0 && row < m_profiles.size();
    m_renameButton->setEnabled(valid);
    m_removeButton->setEnabled(valid);
    if (!valid) {
        m_details->setText(QStringLiteral("Профили пока не добавлены"));
        return;
    }
    const VlessProfile &profile = m_profiles.at(row);
    m_details->setText(QStringLiteral("%1 · %2 · %3")
                           .arg(profile.endpoint(), profile.security.toUpper(), profile.transport.toUpper()));
}

void ProfileManagerDialog::rebuildList(int selectedRow)
{
    m_list->clear();
    for (const VlessProfile &profile : std::as_const(m_profiles)) {
        auto *item = new QListWidgetItem(profile.name, m_list);
        item->setToolTip(profile.endpoint());
    }
    if (selectedRow >= 0 && selectedRow < m_list->count()) {
        m_list->setCurrentRow(selectedRow);
    }
    updateSelection();
}

} // namespace lighttunnel
