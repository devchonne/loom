#include "ui/CheatSheet.h"

#include <QKeyEvent>
#include <QPainter>

namespace {
struct ShortcutRow {
    const char* keys;
    const char* action;
};

const ShortcutRow kRows[] = {
    {"Ctrl+S", "save"},
    {"Ctrl+Shift+S", "save as"},
    {"Ctrl+O", "open"},
    {"Ctrl+N", "new tab"},
    {"Ctrl+W", "close tab"},
    {"Ctrl+Tab", "next tab"},
    {"Shift+click tabs", "compare"},
    {"Ctrl+Shift+Tab", "previous tab"},
    {"Alt+1..9", "jump to tab"},
    {"Ctrl+Shift+[ ]", "move tab"},
    {"Ctrl+R", "rename tab"},
    {"Ctrl+Z / Ctrl+Shift+Z", "undo / redo"},
    {"Ctrl+V", "paste text / image"},
    {"Ctrl+F", "find"},
    {"Ctrl+G / Shift+G", "find next / prev"},
    {"Ctrl+D", "duplicate line"},
    {"Shift+Alt+Up/Dn", "multi cursor"},
    {"Ctrl+B / I / L", "bold / italic / link"},
    {"Ctrl+Wheel", "zoom"},
    {"Ctrl+= / - / 0", "zoom in / out / reset"},
    {"Ctrl+M", "toggle markdown"},
    {"Ctrl+Shift+F", "zen mode"},
    {"Ctrl+Alt+W", "light rain"},
    {"Ctrl+Alt+Shift+W", "storm"},
    {"F11", "fullscreen"},
    {"Ctrl+T", "theme picker"},
    {"Ctrl+Shift+T", "next theme"},
    {"Ctrl+,", "settings"},
    {"Ctrl+K", "this cheat sheet"},
    {"Ctrl+P", "tab switcher"},
    {"Ctrl+Q", "quit"},
    {"/rain 1|0", "light rain"},
    {"/storm 1|0", "storm"},
    {"/sound 1|0", "rain audio"},
    {"/save /zen /theme", "slash + enter"},
    {"Esc", "dismiss overlay"},
};
}

CheatSheet::CheatSheet(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    hide();
}

void CheatSheet::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void CheatSheet::setChromeFont(const QFont& font) {
    setFont(font);
    update();
}

void CheatSheet::toggle() {
    setVisible(!isVisible());
    if (isVisible()) {
        raise();
        setFocus();
    }
}

void CheatSheet::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_K) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CheatSheet::mousePressEvent(QMouseEvent*) {
    hide();
}

void CheatSheet::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));

    const int cardW = qMin(640, width() - 48);
    const int rowH = 22;
    const int rows = int(sizeof(kRows) / sizeof(kRows[0]));
    const int cardH = 56 + rows * rowH;
    const QRect card((width() - cardW) / 2, (height() - cardH) / 2, cardW, cardH);
    p.fillRect(card, theme_.darkBackground);
    p.setPen(theme_.accent);
    p.drawRect(card.adjusted(0, 0, -1, -1));

    QFont title = font();
    title.setPointSizeF(font().pointSizeF() + 2);
    p.setFont(title);
    p.setPen(theme_.accent);
    p.drawText(card.adjusted(20, 12, -20, 0), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("shortcuts"));

    p.setFont(font());
    int y = card.top() + 44;
    for (const ShortcutRow& row : kRows) {
        p.setPen(theme_.accent);
        p.drawText(QRect(card.left() + 20, y, 220, rowH), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromLatin1(row.keys));
        p.setPen(theme_.foreground);
        p.drawText(QRect(card.left() + 250, y, card.width() - 270, rowH), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromLatin1(row.action));
        y += rowH;
    }
}
