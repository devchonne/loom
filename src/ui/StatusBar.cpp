#include "ui/StatusBar.h"

#include <QPainter>

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(22);
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

QSize StatusBar::sizeHint() const {
    return {200, 22};
}

void StatusBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme_.darkBackground);
    p.setPen(theme_.muted);
    p.drawLine(0, 0, width(), 0);
    p.setPen(theme_.darkForeground);
    p.setFont(font());
    p.drawText(rect().adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text_);
    if (!themeName_.isEmpty()) {
        p.setPen(theme_.muted);
        p.drawText(rect().adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignRight, themeName_);
    }
}
