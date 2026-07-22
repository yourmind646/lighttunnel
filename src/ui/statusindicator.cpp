#include "ui/statusindicator.h"

#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QShowEvent>

namespace lighttunnel {

StatusIndicator::StatusIndicator(QWidget *parent)
    : QWidget(parent)
    , m_animation(new QPropertyAnimation(this, "pulse", this))
{
    setFixedSize(64, 64);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setDuration(1800);
    m_animation->setLoopCount(-1);
    m_animation->setEasingCurve(QEasingCurve::InOutSine);
}

void StatusIndicator::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    m_pulse = 0.0;
    updateAnimation();
    update();
}

void StatusIndicator::setPulse(qreal value)
{
    m_pulse = value;
    update();
}

void StatusIndicator::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center(width() / 2.0, height() / 2.0);
    const QColor color = modeColor();

    if (m_mode == Mode::Connected) {
        QColor pulseColor = color;
        pulseColor.setAlphaF(0.24 * (1.0 - m_pulse));
        painter.setPen(QPen(pulseColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        const qreal radius = 20.0 + (8.0 * m_pulse);
        painter.drawEllipse(center, radius, radius);
    }

    QColor halo = color;
    halo.setAlpha(34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(center, 24.0, 24.0);

    QColor plate = color;
    plate.setAlpha(55);
    painter.setBrush(plate);
    painter.drawEllipse(center, 18.0, 18.0);

    if (m_mode == Mode::Busy) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 4.0, Qt::SolidLine, Qt::RoundCap));
        const QRectF arcRect(center.x() - 13.0, center.y() - 13.0, 26.0, 26.0);
        painter.drawArc(arcRect, static_cast<int>(-m_pulse * 360.0 * 16.0), 220 * 16);
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(center, 11.0, 11.0);

    painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (m_mode == Mode::Connected) {
        QPainterPath check;
        check.moveTo(center + QPointF(-5.0, 0.0));
        check.lineTo(center + QPointF(-1.0, 4.0));
        check.lineTo(center + QPointF(6.0, -5.0));
        painter.drawPath(check);
    } else if (m_mode == Mode::Error) {
        painter.drawLine(center + QPointF(0.0, -5.0), center + QPointF(0.0, 2.0));
        painter.drawPoint(center + QPointF(0.0, 6.0));
    } else {
        painter.drawLine(center + QPointF(-4.0, 0.0), center + QPointF(4.0, 0.0));
    }
}

void StatusIndicator::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateAnimation();
}

void StatusIndicator::hideEvent(QHideEvent *event)
{
    m_animation->stop();
    QWidget::hideEvent(event);
}

void StatusIndicator::updateAnimation()
{
    const bool animated = m_mode == Mode::Connected || m_mode == Mode::Busy;
    if (animated && isVisible()) {
        if (m_animation->state() != QAbstractAnimation::Running) {
            m_animation->start();
        }
    } else {
        m_animation->stop();
    }
}

QColor StatusIndicator::modeColor() const
{
    switch (m_mode) {
    case Mode::Connected:
        return QColor(QStringLiteral("#34d399"));
    case Mode::Busy:
        return QColor(QStringLiteral("#fbbf24"));
    case Mode::Error:
        return QColor(QStringLiteral("#fb7185"));
    case Mode::Disconnected:
        return QColor(QStringLiteral("#8491a8"));
    }
    return QColor(QStringLiteral("#8491a8"));
}

} // namespace lighttunnel
