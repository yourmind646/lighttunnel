#include "ui/opaquecombobox.h"

#include <QAbstractItemView>
#include <QColor>
#include <QListView>
#include <QPalette>

namespace lighttunnel {
namespace {

const QString &popupStyleSheet()
{
    static const QString style = QStringLiteral(
        "QWidget { background-color: #121a2d; color: #e7ecf5; }"
        "QAbstractItemView {"
        "  background-color: #121a2d;"
        "  color: #e7ecf5;"
        "  border: 1px solid #34415f;"
        "  padding: 4px;"
        "  outline: 0;"
        "  selection-background-color: #2d3a5d;"
        "  selection-color: #ffffff;"
        "}"
        "QAbstractItemView::item {"
        "  background-color: #121a2d;"
        "  color: #e7ecf5;"
        "  min-height: 30px;"
        "  padding: 3px 8px;"
        "}"
        "QAbstractItemView::item:selected {"
        "  background-color: #2d3a5d;"
        "  color: #ffffff;"
        "}");
    return style;
}

void makeWidgetOpaque(QWidget *widget, const QPalette &palette)
{
    if (widget == nullptr) {
        return;
    }
    widget->setAttribute(Qt::WA_TranslucentBackground, false);
    widget->setAttribute(Qt::WA_NoSystemBackground, false);
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setAutoFillBackground(true);
    widget->setPalette(palette);
}

} // namespace

OpaqueComboBox::OpaqueComboBox(QWidget *parent)
    : QComboBox(parent)
{
    // Supplying our own view keeps native GTK/Adwaita popup styling from
    // replacing the application palette on GNOME.
    setView(new QListView(this));
    applyPopupAppearance();
}

void OpaqueComboBox::showPopup()
{
    // The private popup container is created lazily.  Style both before and
    // after showing it so its Wayland surface is born opaque and remains so if
    // the platform theme reapplies its palette during Show.
    applyPopupAppearance();
    QComboBox::showPopup();
    applyPopupAppearance();
}

void OpaqueComboBox::applyPopupAppearance()
{
    QAbstractItemView *popupView = view();
    QPalette palette = popupView->palette();
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#121a2d")));
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#121a2d")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e7ecf5")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2d3a5d")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));

    QWidget *container = popupView->window();
    makeWidgetOpaque(container, palette);
    makeWidgetOpaque(popupView, palette);
    makeWidgetOpaque(popupView->viewport(), palette);
    container->setWindowOpacity(1.0);
    container->setStyleSheet(popupStyleSheet());
}

} // namespace lighttunnel
