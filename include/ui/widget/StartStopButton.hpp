#pragma once

#include <QToolButton>
#include <QColor>

class QPropertyAnimation;

// A self-painted start/stop control for the main toolbar.
//
// It keeps the standard QToolButton chrome (frame, background, hover/pressed)
// so it sits naturally next to the other toolbar buttons, and paints a custom
// indicator on top: a central glyph that morphs between a play triangle and a
// stop square, wrapped by a ring that carries the active proxy-mode colour and
// doubles as a spinner while connecting.
//
// It is a thin view over the app's profile state: the owner pushes a State
// (Disabled / Idle / Connecting / Running) and a Mode (the active proxy mode).
class StartStopButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(qreal morph READ morph WRITE setMorph)
    Q_PROPERTY(qreal spin READ spin WRITE setSpin)
    Q_PROPERTY(qreal glow READ glow WRITE setGlow)
    Q_PROPERTY(qreal dim READ dim WRITE setDim)
    Q_PROPERTY(qreal press READ press WRITE setPress)
    Q_PROPERTY(QColor ringColor READ ringColor WRITE setRingColor)

public:
    enum class State { Disabled, Idle, Connecting, Running };
    Q_ENUM(State)

    // Mirrors the tray-icon modes computed in MainWindow::refresh_status.
    enum class Mode { Off, Core, SystemProxy, Tun, Dns, SystemProxyDns };
    Q_ENUM(Mode)

    explicit StartStopButton(QWidget *parent = nullptr);

    void setState(State s);
    State state() const { return m_state; }

    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    // --- animated properties ---
    qreal morph() const { return m_morph; }
    void setMorph(qreal v) { m_morph = v; update(); }
    qreal spin() const { return m_spin; }
    void setSpin(qreal v) { m_spin = v; update(); }
    qreal glow() const { return m_glow; }
    void setGlow(qreal v) { m_glow = v; update(); }
    qreal dim() const { return m_dim; }
    void setDim(qreal v) { m_dim = v; update(); }
    qreal press() const { return m_press; }
    void setPress(qreal v) { m_press = v; update(); }
    QColor ringColor() const { return m_ringColor; }
    void setRingColor(const QColor &c) { m_ringColor = c; update(); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void applyState(bool animated);
    void animate(QPropertyAnimation *anim, const QVariant &to, int duration);
    void setLoopRunning(QPropertyAnimation *anim, bool running);

    QColor modeColor(Mode m) const;
    QColor idleRingColor() const;
    QColor glyphColor() const;
    QColor targetRingColor() const;

    State m_state = State::Idle; // forced to Disabled in the constructor
    Mode m_mode = Mode::Off;

    qreal m_morph = 0.0; // 0 = play triangle, 1 = stop square
    qreal m_spin = 0.0;  // connecting arc rotation, degrees
    qreal m_glow = 0.0;  // running "breathing" inner glow, 0..1
    qreal m_dim = 1.0;   // indicator opacity multiplier (low while disabled)
    qreal m_press = 0.0; // press depth, 0..1
    QColor m_ringColor;

    QPropertyAnimation *m_morphAnim = nullptr;
    QPropertyAnimation *m_dimAnim = nullptr;
    QPropertyAnimation *m_pressAnim = nullptr;
    QPropertyAnimation *m_ringColorAnim = nullptr;
    QPropertyAnimation *m_spinAnim = nullptr;
    QPropertyAnimation *m_glowAnim = nullptr;
};
