#pragma once

#include <QColor>
#include <QWidget>

class QHideEvent;
class QPropertyAnimation;
class QShowEvent;

namespace lighttunnel {

class StatusIndicator final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulse READ pulse WRITE setPulse)

public:
    enum class Mode {
        Disconnected,
        Busy,
        Connected,
        Error,
    };

    explicit StatusIndicator(QWidget *parent = nullptr);

    void setMode(Mode mode);
    [[nodiscard]] qreal pulse() const noexcept { return m_pulse; }
    void setPulse(qreal value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateAnimation();
    [[nodiscard]] QColor modeColor() const;

    Mode m_mode{Mode::Disconnected};
    qreal m_pulse{0.0};
    QPropertyAnimation *m_animation{};
};

} // namespace lighttunnel
