#include "ui/CheatSheet.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTabBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr char kAll[] = "all";

struct RawEntry {
    const char* section;
    const char* keys;
    const char* action;
};

const RawEntry kRaw[] = {
    {"files", "Ctrl+S", "save"},
    {"files", "Ctrl+Shift+S", "save as"},
    {"files", "Ctrl+O", "open file"},
    {"files", "Ctrl+,", "settings"},
    {"files", "Ctrl+Q", "quit loom"},

    {"tabs", "Ctrl+N", "new tab"},
    {"tabs", "Ctrl+W", "close tab"},
    {"tabs", "Ctrl+Tab", "next tab"},
    {"tabs", "Ctrl+Shift+Tab", "previous tab"},
    {"tabs", "Alt+1..9", "jump to tab"},
    {"tabs", "Ctrl+Shift+[ / ]", "move tab left / right"},
    {"tabs", "Ctrl+R", "rename tab"},
    {"tabs", "Ctrl+P", "tab switcher"},
    {"tabs", "Shift+click tab", "compare two tabs side by side"},

    {"edit", "Ctrl+Z", "undo"},
    {"edit", "Ctrl+Shift+Z", "redo"},
    {"edit", "Ctrl+V", "paste text or image"},
    {"edit", "Ctrl+D", "duplicate line"},
    {"edit", "Shift+Alt+Up / Down", "add multi cursor"},
    {"edit", "Ctrl+B", "bold"},
    {"edit", "Ctrl+I", "italic"},
    {"edit", "Ctrl+L", "link"},
    {"edit", "Ctrl+F", "find in document"},
    {"edit", "Ctrl+G", "find next"},
    {"edit", "Ctrl+Shift+G", "find previous"},

    {"tables", "Ctrl+Shift+\\", "align table under caret"},
    {"tables", "Tab / Shift+Tab", "next / previous cell"},
    {"tables", "Enter", "insert row (blank row exits table)"},
    {"tables", "Ctrl+Shift+Down", "insert table row"},
    {"tables", "Ctrl+Shift+Up", "delete table row"},
    {"tables", "Ctrl+Shift+Right", "insert table column"},
    {"tables", "Ctrl+Shift+Left", "delete table column"},
    {"tables", "Ctrl+Enter", "line break inside a cell"},

    {"navigate", "Ctrl+Shift+O", "outline overlay (jump to heading)"},
    {"navigate", "click heading link", "jump to that heading"},
    {"navigate", "Ctrl+Enter", "follow link under caret"},
    {"navigate", "Ctrl+click", "open external url"},
    {"navigate", "Alt+Left", "jump back"},

    {"view", "Ctrl+Wheel", "zoom"},
    {"view", "Ctrl+= / Ctrl+-", "zoom in / out"},
    {"view", "Ctrl+0", "reset zoom"},
    {"view", "Ctrl+M", "toggle markdown formatting"},
    {"view", "Ctrl+Shift+F", "zen mode"},
    {"view", "F11", "fullscreen"},
    {"view", "Ctrl+Alt+W", "light rain"},
    {"view", "Ctrl+Alt+Shift+W", "storm"},
    {"view", "Ctrl+T", "theme picker"},
    {"view", "Ctrl+Shift+T", "next theme"},
    {"view", "Ctrl+K", "this cheat sheet"},
    {"view", "Esc", "dismiss overlay"},

    {"slash", "/save, /saveas", "save / save as"},
    {"slash", "/open, /new, /close", "file and tab actions"},
    {"slash", "/rename", "rename current tab"},
    {"slash", "/quit, /exit", "quit loom"},
    {"slash", "/zen 1|0", "zen mode"},
    {"slash", "/md 1|0", "toggle markdown formatting"},
    {"slash", "/fullscreen 1|0", "fullscreen"},
    {"slash", "/zoom [percent]", "set or reset zoom"},
    {"slash", "/rain 1|0", "light rain"},
    {"slash", "/storm 1|0", "storm"},
    {"slash", "/sound 1|0", "rain audio"},
    {"slash", "/toc [depth]", "insert or refresh table of contents"},
    {"slash", "/toc list, /outline", "outline overlay"},
    {"slash", "/table [NxM]", "insert table skeleton"},
    {"slash", "/table align", "align table under caret"},
    {"slash", "/table row|col", "insert table row / column"},
    {"slash", "/table delrow|delcol", "delete table row / column"},
    {"slash", "/find [text]", "find in document"},
    {"slash", "/tabs", "tab switcher"},
    {"slash", "/theme [name]", "theme picker or pick a palette"},
    {"slash", "/settings", "open settings"},
    {"slash", "/help, /keys", "this cheat sheet"},
};

}  // namespace

namespace ShortcutCatalog {

QString allSection() {
    return QString::fromLatin1(kAll);
}

const QVector<ShortcutEntry>& entries() {
    static const QVector<ShortcutEntry> cached = []() {
        QVector<ShortcutEntry> out;
        out.reserve(int(sizeof(kRaw) / sizeof(kRaw[0])));
        for (const RawEntry& raw : kRaw) {
            out.append(ShortcutEntry{QString::fromUtf8(raw.keys),
                                     QString::fromUtf8(raw.action),
                                     QString::fromLatin1(raw.section)});
        }
        return out;
    }();
    return cached;
}

QStringList sections() {
    QStringList out;
    for (const ShortcutEntry& entry : entries()) {
        if (!out.contains(entry.section)) {
            out.append(entry.section);
        }
    }
    return out;
}

QStringList tokenize(const QString& query) {
    QStringList tokens;
    for (const QString& part : query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        tokens.append(part.toLower());
    }
    return tokens;
}

bool matches(const ShortcutEntry& entry, const QStringList& tokens) {
    if (tokens.isEmpty()) {
        return true;
    }
    const QString haystack =
        (entry.keys + QLatin1Char(' ') + entry.action + QLatin1Char(' ') + entry.section).toLower();
    for (const QString& token : tokens) {
        if (!haystack.contains(token)) {
            return false;
        }
    }
    return true;
}

QVector<ShortcutEntry> search(const QString& query, const QString& section) {
    const QStringList tokens = tokenize(query);
    const bool global = section.isEmpty() || section == allSection();
    QVector<ShortcutEntry> out;
    for (const ShortcutEntry& entry : entries()) {
        if (!global && entry.section != section) {
            continue;
        }
        if (matches(entry, tokens)) {
            out.append(entry);
        }
    }
    return out;
}

int count(const QString& query, const QString& section) {
    return int(search(query, section).size());
}

}  // namespace ShortcutCatalog

namespace {
constexpr int kCardWidth = 700;
constexpr int kKeysColumn = 240;
constexpr int kSectionRole = Qt::UserRole + 1;
}  // namespace

CheatSheet::CheatSheet(QWidget* parent)
    : QWidget(parent)
    , card_(new QWidget(this))
    , title_(new QLabel(QStringLiteral("shortcuts"), this))
    , hint_(new QLabel(QStringLiteral("tab: section   esc: close"), this))
    , footer_(new QLabel(this))
    , search_(new QLineEdit(this))
    , tabs_(new QTabBar(this))
    , tree_(new QTreeWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();

    card_->setObjectName(QStringLiteral("switcherCard"));
    card_->setFixedWidth(kCardWidth);
    auto* cardLayout = new QVBoxLayout(card_);
    cardLayout->setSpacing(8);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(title_);
    header->addStretch();
    header->addWidget(hint_);
    cardLayout->addLayout(header);

    search_->setPlaceholderText(QStringLiteral("search every shortcut and slash command"));
    search_->setClearButtonEnabled(false);
    search_->installEventFilter(this);
    cardLayout->addWidget(search_);

    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setFocusPolicy(Qt::NoFocus);
    tabs_->setUsesScrollButtons(true);
    cardLayout->addWidget(tabs_);

    tree_->setColumnCount(2);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(false);
    tree_->setIndentation(10);
    tree_->setUniformRowHeights(true);
    tree_->setFocusPolicy(Qt::NoFocus);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree_->setFixedHeight(360);
    tree_->setColumnWidth(0, kKeysColumn);
    cardLayout->addWidget(tree_);

    cardLayout->addWidget(footer_);

    layout->addWidget(card_, 0, Qt::AlignHCenter);
    layout->addStretch();

    buildTabs();
    connect(search_, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(tabs_, &QTabBar::currentChanged, this, [this](int) { rebuild(); });
    restyle();
    rebuild();
    hide();
}

void CheatSheet::buildTabs() {
    tabs_->addTab(ShortcutCatalog::allSection());
    tabs_->setTabData(0, ShortcutCatalog::allSection());
    for (const QString& section : ShortcutCatalog::sections()) {
        const int index = tabs_->addTab(section);
        tabs_->setTabData(index, section);
    }
}

QString CheatSheet::currentSection() const {
    const QVariant data = tabs_->tabData(tabs_->currentIndex());
    return data.isValid() ? data.toString() : ShortcutCatalog::allSection();
}

void CheatSheet::setTheme(const Theme& theme) {
    theme_ = theme;
    restyle();
    rebuild();
    update();
}

void CheatSheet::setChromeFont(const QFont& font) {
    setFont(font);
    search_->setFont(font);
    tabs_->setFont(font);
    tree_->setFont(font);
    footer_->setFont(font);
    hint_->setFont(font);
    QFont titleFont = font;
    titleFont.setPointSizeF(font.pointSizeF() + 2);
    title_->setFont(titleFont);
    update();
}

void CheatSheet::restyle() {
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.accent.name()));
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.muted.name()));
    footer_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.muted.name()));
    tabs_->setStyleSheet(QStringLiteral(R"(
QTabBar::tab {
    background: %1;
    color: %2;
    border: 1px solid %1;
    padding: 3px 10px;
    margin-right: 2px;
}
QTabBar::tab:hover {
    color: %3;
}
QTabBar::tab:selected {
    background: %4;
    color: %5;
    border-color: %3;
}
)")
                              .arg(theme_.darkerBackground.name(),
                                   theme_.muted.name(),
                                   theme_.accent.name(),
                                   theme_.selection.name(),
                                   theme_.brightForeground.name()));
}

void CheatSheet::open() {
    search_->clear();
    if (tabs_->currentIndex() != 0) {
        tabs_->setCurrentIndex(0);
    }
    rebuild();
    show();
    raise();
    search_->setFocus();
}

void CheatSheet::toggle() {
    if (isVisible()) {
        hide();
        return;
    }
    open();
}

void CheatSheet::dismiss() {
    if (!search_->text().isEmpty()) {
        search_->clear();
        return;
    }
    hide();
}

void CheatSheet::rebuild() {
    const QString query = search_->text().trimmed();
    QString section = currentSection();
    const QString all = ShortcutCatalog::allSection();
    const int globalHits = ShortcutCatalog::count(query);

    // Global search: never dead-end inside a section the query does not touch.
    if (!query.isEmpty() && section != all && ShortcutCatalog::count(query, section) == 0
        && globalHits > 0) {
        const QSignalBlocker block(tabs_);
        tabs_->setCurrentIndex(0);
        section = all;
    }

    for (int i = 0; i < tabs_->count(); ++i) {
        const QString id = tabs_->tabData(i).toString();
        if (query.isEmpty()) {
            tabs_->setTabText(i, id);
        } else {
            tabs_->setTabText(i, QStringLiteral("%1 %2").arg(id).arg(
                                     id == all ? globalHits : ShortcutCatalog::count(query, id)));
        }
    }

    tree_->clear();
    const QVector<ShortcutEntry> hits = ShortcutCatalog::search(query, section);
    QTreeWidgetItem* group = nullptr;
    QString groupSection;
    QTreeWidgetItem* firstLeaf = nullptr;
    for (const ShortcutEntry& entry : hits) {
        if (!group || entry.section != groupSection) {
            groupSection = entry.section;
            group = new QTreeWidgetItem(tree_);
            group->setText(0, groupSection);
            group->setFirstColumnSpanned(true);
            group->setFlags(Qt::ItemIsEnabled);
            group->setData(0, kSectionRole, groupSection);
            QFont groupFont = tree_->font();
            groupFont.setBold(true);
            group->setFont(0, groupFont);
            group->setForeground(0, theme_.accent);
        }
        auto* item = new QTreeWidgetItem(group);
        item->setText(0, entry.keys);
        item->setText(1, entry.action);
        item->setForeground(0, theme_.brightForeground);
        item->setForeground(1, theme_.foreground);
        if (!firstLeaf) {
            firstLeaf = item;
        }
    }
    tree_->expandAll();
    tree_->setColumnWidth(0, kKeysColumn);
    if (firstLeaf) {
        tree_->setCurrentItem(firstLeaf);
        tree_->scrollToTop();
    }

    if (hits.isEmpty()) {
        footer_->setText(QStringLiteral("no matches for \"%1\"").arg(query));
        return;
    }
    QString text = QStringLiteral("%1 of %2").arg(hits.size()).arg(ShortcutCatalog::entries().size());
    if (!query.isEmpty() && section != all) {
        const int elsewhere = globalHits - int(hits.size());
        if (elsewhere > 0) {
            text += QStringLiteral("  ·  %1 more in other sections (all)").arg(elsewhere);
        }
    }
    footer_->setText(text);
}

void CheatSheet::moveSelection(int delta) {
    QVector<QTreeWidgetItem*> leaves;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if ((*it)->parent()) {
            leaves.append(*it);
        }
    }
    if (leaves.isEmpty()) {
        return;
    }
    int index = leaves.indexOf(tree_->currentItem());
    if (index < 0) {
        index = delta > 0 ? -1 : 0;
    }
    const int next = qBound(0, index + delta, int(leaves.size()) - 1);
    tree_->setCurrentItem(leaves.at(next));
    tree_->scrollToItem(leaves.at(next));
}

void CheatSheet::cycleSection(int delta) {
    const int n = tabs_->count();
    if (n == 0) {
        return;
    }
    tabs_->setCurrentIndex((tabs_->currentIndex() + delta + n) % n);
}

bool CheatSheet::handleNavKey(QKeyEvent* event) {
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    if (ctrl && event->key() == Qt::Key_K) {
        // Safety net: keep Ctrl+K a toggle even while the search field has focus.
        hide();
        return true;
    }
    switch (event->key()) {
    case Qt::Key_Escape:
        dismiss();
        return true;
    case Qt::Key_Tab:
        cycleSection(1);
        return true;
    case Qt::Key_Backtab:
        cycleSection(-1);
        return true;
    case Qt::Key_Down:
        moveSelection(1);
        return true;
    case Qt::Key_Up:
        moveSelection(-1);
        return true;
    case Qt::Key_PageDown:
        moveSelection(10);
        return true;
    case Qt::Key_PageUp:
        moveSelection(-10);
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (ctrl) {
            cycleSection(event->key() == Qt::Key_Right ? 1 : -1);
            return true;
        }
        return false;
    case Qt::Key_Home:
    case Qt::Key_End:
        if (ctrl) {
            moveSelection(event->key() == Qt::Key_End ? tree_->topLevelItemCount() + 1000 : -100000);
            return true;
        }
        return false;
    default:
        break;
    }
    return false;
}

bool CheatSheet::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        if (handleNavKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CheatSheet::keyPressEvent(QKeyEvent* event) {
    if (handleNavKey(event)) {
        return;
    }
    QWidget::keyPressEvent(event);
}

void CheatSheet::mousePressEvent(QMouseEvent* event) {
    if (card_ && card_->geometry().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }
    hide();
}

void CheatSheet::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Keep the card inside small windows / zen mode instead of clipping it.
    card_->setFixedWidth(qMax(360, qMin(kCardWidth, width() - 48)));
    tree_->setFixedHeight(qBound(160, height() - 220, 520));
    tree_->setColumnWidth(0, kKeysColumn);
}

void CheatSheet::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit closed();
}

void CheatSheet::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
