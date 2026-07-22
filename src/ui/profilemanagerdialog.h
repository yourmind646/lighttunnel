#pragma once

#include "core/profilerepository.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QPushButton;

namespace lighttunnel {

class ProfileManagerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ProfileManagerDialog(QVector<VlessProfile> profiles, QWidget *parent = nullptr);

    [[nodiscard]] QVector<VlessProfile> profiles() const { return m_profiles; }

private slots:
    void importProfiles();
    void importSubscription();
    void renameProfile();
    void removeProfile();
    void updateSelection();

private:
    void rebuildList(int selectedRow = -1);
    void importPayload(const QByteArray &payload, const QString &sourceName);

    QVector<VlessProfile> m_profiles;
    QListWidget *m_list{};
    QLabel *m_details{};
    QPushButton *m_renameButton{};
    QPushButton *m_removeButton{};
    QNetworkAccessManager *m_network{};
};

} // namespace lighttunnel
