#include "ui/StatusBar.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

namespace {

// Glyph box and the gaps around the control, all in device-independent pixels.
constexpr int kGlyphSize = 9;
constexpr int kGlyphGap = 8;
constexpr int kBandGap = 14;
constexpr int kEdgeInset = 10;

}  // namespace

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(22);
    setMouseTracking(true);
}

void StatusBar::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void StatusBar::setChromeFont(const QFont& font) {
    QFont f = font;
    f.setPointSizeF(9.0);
    setFont(f);
    update();
}

void StatusBar::setInfo(const QString& title, int words, int line, int column, bool dirty, int zoom,
                        const QString& themeName) {
    text_ = QStringLiteral("%1  ·  %2 words  ·  ln %3 col %4  ·  %5%%  %6")
                .arg(title)
                .arg(words)
                .arg(line)
                .arg(column)
                .arg(zoom)
                .arg(dirty ? QStringLiteral("●") : QStringLiteral("○"));
    themeName_ = themeName;
    update();
}

void StatusBar::setRadio(bool visible, RadioState state) {
    if (radioVisible_ == visible && radioState_ == state) {
        return;
    }
    radioVisible_ = visible;
    radioState_ = state;
    if (!radioVisible_) {
        hover_ = Hit::None;
        setCursor(Qt::ArrowCursor);
    }
    update();
}

QSize StatusBar::sizeHint() const {
    return {200, 22};
}

int StatusBar::radioBandWidth() const {
    if (!radioVisible_) {
        return 0;
    }
    return kGlyphSize * 2 + kGlyphGap + kBandGap;
}

QRect StatusBar::stopRect() const {
    if (!radioVisible_) {
        return {};
    }
    // Laid out right to left, immediately left of the theme name.
    const int themeWidth =
        themeName_.isEmpty() ? 0 : QFontMetrics(font()).horizontalAdvance(themeName_) + kBandGap;
    const int right = width() - kEdgeInset - themeWidth;
    const int top = (height() - kGlyphSize) / 2;
    return {right - kGlyphSize, top, kGlyphSize, kGlyphSize};
}

QRect StatusBar::toggleRect() const {
    const QRect stop = stopRect();
    if (stop.isNull()) {
        return {};
    }
    return {stop.left() - kGlyphGap - kGlyphSize, stop.top(), kGlyphSize, kGlyphSize};
}

StatusBar::Hit StatusBar::hitTest(const QPoint& pos) const {
    if (!radioVisible_) {
        return Hit::None;
    }
    // Grow the targets vertically: 9px is too small to click reliably in a 22px
    // bar, so each glyph claims the full height of its column.
    const QRect toggle = toggleRect().adjusted(-2, -(height() / 2), 2, height() / 2);
    const QRect stop = stopRect().adjusted(-2, -(height() / 2), 2, height() / 2);
    if (toggle.contains(pos)) {
        return Hit::Toggle;
    }
    if (stop.contains(pos)) {
        return Hit::Stop;
    }
    // The padding around the glyphs reopens the dialog.
    const QRect band(toggleRect().left() - kBandGap / 2, 0,
                     radioBandWidth() + kBandGap / 2, height());
    if (band.contains(pos)) {
        return Hit::Area;
    }
    return Hit::None;
}

QColor StatusBar::radioColor() const {
    switch (radioState_) {
    case RadioState::Playing:
        return theme_.green;
    case RadioState::Connecting:
    case RadioState::Reconnecting:
        return theme_.yellow;
    case RadioState::Error:
        return theme_.red;
    case RadioState::Paused:
    case RadioState::Idle:
        break;
    }
    return theme_.muted;
}

void StatusBar::drawToggleGlyph(QPainter& p, const QRect& box) const {
    const bool playing =
        radioState_ == RadioState::Playing || radioState_ == RadioState::Connecting
        || radioState_ == RadioState::Reconnecting;
    if (playing) {
        // Pause: two bars.
        const int barWidth = qMax(2, box.width() / 3 - 1);
        p.fillRect(QRect(box.left(), box.top(), barWidth, box.height()), p.pen().color());
        p.fillRect(QRect(box.right() - barWidth + 1, box.top(), barWidth, box.height()),
                   p.pen().color());
        return;
    }
    // Play: a right-pointing triangle.
    QPolygonF triangle;
    triangle << QPointF(box.left(), box.top()) << QPointF(box.left(), box.bottom() + 1)
             << QPointF(box.right() + 1, box.center().y() + 0.5);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(p.pen().color());
    p.setPen(Qt::NoPen);
    p.drawPolygon(triangle);
    p.restore();
}

void StatusBar::drawStopGlyph(QPainter& p, const QRect& box) const {
    // Stop: a filled square, inset slightly so it reads lighter than the bars.
    p.fillRect(box.adjusted(1, 1, -1, -1), p.pen().color());
}

void StatusBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme_.darkBackground);
    p.setPen(theme_.muted);
    p.drawLine(0, 0, width(), 0);
    p.setFont(font());

    // Reserve the right-hand side before drawing the left text, otherwise a long
    // filename overdraws the theme name and the mini player.
    const int themeWidth =
        themeName_.isEmpty() ? 0 : QFontMetrics(font()).horizontalAdvance(themeName_) + kBandGap;
    const int reserved = themeWidth + radioBandWidth();
    p.setPen(theme_.darkForeground);
    p.drawText(rect().adjusted(kEdgeInset, 0, -kEdgeInset - reserved, 0),
               Qt::AlignVCenter | Qt::AlignLeft, text_);

    if (!themeName_.isEmpty()) {
        p.setPen(theme_.muted);
        p.drawText(rect().adjusted(kEdgeInset, 0, -kEdgeInset, 0),
                   Qt::AlignVCenter | Qt::AlignRight, themeName_);
    }

    if (!radioVisible_) {
        return;
    }
    const QColor base = radioColor();
    p.setPen(hover_ == Hit::Toggle ? theme_.accent : base);
    drawToggleGlyph(p, toggleRect());
    p.setPen(hover_ == Hit::Stop ? theme_.accent
                                 : (radioState_ == RadioState::Idle ? theme_.muted : base));
    drawStopGlyph(p, stopRect());
}

void StatusBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    switch (hitTest(event->pos())) {
    case Hit::Toggle:
        emit radioToggleClicked();
        return;
    case Hit::Stop:
        emit radioStopClicked();
        return;
    case Hit::Area:
        emit radioOpenClicked();
        return;
    case Hit::None:
        break;
    }
    QWidget::mousePressEvent(event);
}

void StatusBar::mouseMoveEvent(QMouseEvent* event) {
    const Hit hit = hitTest(event->pos());
    if (hit != hover_) {
        hover_ = hit;
        setCursor(hover_ == Hit::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void StatusBar::leaveEvent(QEvent* event) {
    if (hover_ != Hit::None) {
        hover_ = Hit::None;
        setCursor(Qt::ArrowCursor);
        update();
    }
    QWidget::leaveEvent(event);
}
