#pragma once

#include "theme/Theme.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

struct ShortcutEntry {
    QString keys;
    QString action;
    QString section;
};

// Pure data + filtering helpers, kept free of widget state so they can be unit tested.
namespace ShortcutCatalog {

// Pseudo-section meaning "search/browse everything".
QString allSection();

// Every known shortcut / slash command, ordered by section.
const QVector<ShortcutEntry>& entries();

// Section ids in tab order (does not include allSection()).
QStringList sections();

// Splits a query into lowercase tokens.
QStringList tokenize(const QString& query);

// True when every token matches the keys, action or section text.
bool matches(const ShortcutEntry& entry, const QStringList& tokens);

// Filters the catalogue. An empty or allSection() section searches globally.
QVector<ShortcutEntry> search(const QString& query, const QString& section = QString());

// Number of matches for a query inside one section (or globally).
int count(const QString& query, const QString& section = QString());

}  // namespace ShortcutCatalog

class CheatSheet : public QWidget {
    Q_OBJECT

public:
    explicit CheatSheet(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void open();
    void toggle();
    // Clears the query first, closes on a second call.
    void dismiss();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildTabs();
    void rebuild();
    void moveSelection(int delta);
    void cycleSection(int delta);
    bool handleNavKey(QKeyEvent* event);
    QString currentSection() const;
    void restyle();

    QWidget* card_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* footer_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    Theme theme_ = Theme::builtin();
};
