#pragma once

#include <QComboBox>

namespace lighttunnel {

// QComboBox creates its popup as a separate top-level window.  Some GNOME Qt
// platform themes mark that window translucent, so an application-wide style
// sheet alone cannot guarantee an opaque menu background.
class OpaqueComboBox final : public QComboBox {
public:
    explicit OpaqueComboBox(QWidget *parent = nullptr);

protected:
    void showPopup() override;

private:
    void applyPopupAppearance();
};

} // namespace lighttunnel
