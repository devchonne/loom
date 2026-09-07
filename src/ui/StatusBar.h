#pragma once

#include "core/RadioPlayer.h"
#include "theme/Theme.h"

#include <QRect>
#include <QWidget>

#include <cstdint>

class StatusBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void setInfo(const QString& title, int words, int line, int column, bool dirty, int zoom,
                 const QString& themeName = {});
    // The mini player: play/pause and stop, icon-only, sitting just left of the
    // theme name. Hidden entirely when visible is false, which keeps the footer
    // exactly as it was for anyone with no saved stations.
    void setRadio(bool visible, RadioState state);

signals:
    void radioToggleClicked();
    void radioStopClicked();
    // Clicking the control area outside the glyphs reopens the radio dialog.
    void radioOpenClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    QSize sizeHint() const override;

private:
    // Which part of the mini player a point falls on.
    enum class Hit : std::uint8_t { None, Toggle, Stop, Area };

    // Width the whole control reserves, including its trailing gap. Zero when
    // the mini player is hidden.
    int radioBandWidth() const;
    QRect toggleRect() const;
    QRect stopRect() const;
    Hit hitTest(const QPoint& pos) const;
    void drawToggleGlyph(QPainter& p, const QRect& box) const;
    void drawStopGlyph(QPainter& p, const QRect& box) const;
    QColor radioColor() const;

    Theme theme_ = Theme::builtin();
    QString text_;
    QString themeName_;
    bool radioVisible_ = false;
    RadioState radioState_ = RadioState::Idle;
    Hit hover_ = Hit::None;
};
