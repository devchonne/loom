#include "theme/ThemeManager.h"

#include "core/Paths.h"
#include "core/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPalette>

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString&) { reload(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) { reload(); });
}

void ThemeManager::apply(const Settings& settings) {
    source_ = settings.themeSource;
    customPath_ = settings.customThemePath;
    watchOmarchy();
    reload();
}

void ThemeManager::watchOmarchy() {
    const QStringList current = watcher_.files() + watcher_.directories();
    if (!current.isEmpty()) {
        watcher_.removePaths(current);
    }
    if (!Palettes::isOmarchy(source_)) {
        return;
    }
    const QString colors = Paths::omarchyThemeFile();
    const QString name = Paths::omarchyThemeNameFile();
    if (QFile::exists(colors)) {
        watcher_.addPath(colors);
    }
    if (QFile::exists(name)) {
        watcher_.addPath(name);
    }
    const QFileInfo info(colors);
    if (info.dir().exists()) {
        watcher_.addPath(info.absolutePath());
    }
}

void ThemeManager::reload() {
    Theme next = Theme::builtin();
    const QString source = Palettes::normalize(source_);
    if (source == QLatin1String("custom") && !customPath_.isEmpty()) {
        next = OmarchyThemeSource::fromFile(customPath_);
    } else if (Palettes::isOmarchy(source)) {
        const QString path = Paths::omarchyThemeFile();
        if (QFile::exists(path)) {
            next = OmarchyThemeSource::fromFile(path);
        }
        watchOmarchy();
    } else {
        next = Palettes::byId(source);
    }
    theme_ = next;
    emit themeChanged();
}

QPalette ThemeManager::palette() const {
    const Theme& t = theme_;
    QPalette pal;
    pal.setColor(QPalette::Window, t.background);
    pal.setColor(QPalette::WindowText, t.foreground);
    pal.setColor(QPalette::Base, t.darkBackground);
    pal.setColor(QPalette::AlternateBase, t.lighterBackground);
    pal.setColor(QPalette::Text, t.foreground);
    pal.setColor(QPalette::Button, t.darkBackground);
    pal.setColor(QPalette::ButtonText, t.foreground);
    pal.setColor(QPalette::BrightText, t.brightForeground);
    pal.setColor(QPalette::Highlight, t.selection);
    pal.setColor(QPalette::HighlightedText, t.brightForeground);
    pal.setColor(QPalette::Link, t.accent);
    pal.setColor(QPalette::LinkVisited, t.magenta);
    pal.setColor(QPalette::ToolTipBase, t.darkBackground);
    pal.setColor(QPalette::ToolTipText, t.foreground);
    pal.setColor(QPalette::PlaceholderText, t.muted);
    pal.setColor(QPalette::Light, t.lighterBackground);
    pal.setColor(QPalette::Midlight, t.lighterBackground);
    pal.setColor(QPalette::Mid, t.muted);
    pal.setColor(QPalette::Dark, t.darkerBackground);
    pal.setColor(QPalette::Shadow, t.darkerBackground);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.muted);
    pal.setColor(QPalette::Disabled, QPalette::Text, t.muted);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, t.muted);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, t.lighterBackground);
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, t.muted);
    return pal;
}

QString ThemeManager::styleSheet() const {
    const Theme& t = theme_;
    return QStringLiteral(R"(
QMainWindow, QDialog, QFileDialog, QWidget#loomRoot {
    background: %1;
    color: %2;
    font-family: "Departure Mono", "JetBrainsMono Nerd Font", monospace;
}
QScrollBar:vertical {
    background: %1;
    width: 8px;
    margin: 0;
    border: none;
}
QScrollBar::handle:vertical {
    background: %3;
    min-height: 24px;
    border-radius: 0;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    height: 0;
    background: none;
}
QScrollBar:horizontal {
    background: %1;
    height: 8px;
    margin: 0;
    border: none;
}
QScrollBar::handle:horizontal {
    background: %3;
    min-width: 24px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    width: 0;
    background: none;
}
QMenu {
    background: %4;
    color: %2;
    border: 1px solid %3;
    padding: 4px;
}
QMenu::item:selected {
    background: %5;
    color: %6;
}
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: %4;
    color: %2;
    border: 1px solid %3;
    padding: 4px 8px;
    selection-background-color: %5;
    selection-color: %6;
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
    color: %3;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background: %4;
    color: %2;
    border: 1px solid %3;
    selection-background-color: %5;
    selection-color: %6;
    outline: none;
}
QPushButton {
    background: %4;
    color: %2;
    border: 1px solid %3;
    padding: 4px 12px;
}
QPushButton:hover, QPushButton:focus {
    border-color: %7;
    color: %6;
}
QPushButton:disabled, QToolButton:disabled {
    color: %3;
    border-color: %8;
}
QDialogButtonBox QPushButton {
    min-width: 72px;
}
QToolButton {
    background: %4;
    color: %2;
    border: 1px solid %3;
    padding: 4px 8px;
}
QToolButton:hover, QToolButton:focus, QToolButton:pressed {
    border-color: %7;
    color: %6;
}
QCheckBox, QLabel, QSlider {
    color: %2;
}
QListWidget, QTreeView, QListView, QTableView, QAbstractItemView {
    background: %4;
    color: %2;
    border: 1px solid %3;
    outline: none;
    alternate-background-color: %9;
    selection-background-color: %5;
    selection-color: %6;
}
QListWidget::item:selected, QTreeView::item:selected, QListView::item:selected,
QTableView::item:selected {
    background: %5;
    color: %6;
}
QTreeView::item:hover, QListView::item:hover, QListWidget::item:hover {
    background: %9;
}
QHeaderView, QHeaderView::section {
    background: %8;
    color: %2;
    border: none;
    border-bottom: 1px solid %3;
    padding: 4px 8px;
}
QSplitter::handle {
    background: %3;
}
QSplitter::handle:horizontal {
    width: 1px;
}
QSplitter::handle:vertical {
    height: 1px;
}
QWidget#switcherCard {
    background: %4;
    border: 1px solid %7;
}
QWidget#findBar {
    background: %4;
    border-top: 1px solid %3;
}
QToolTip {
    background: %4;
    color: %2;
    border: 1px solid %7;
}
)").arg(t.background.name(),
        t.foreground.name(),
        t.muted.name(),
        t.darkBackground.name(),
        t.selection.name(),
        t.brightForeground.name(),
        t.accent.name(),
        t.darkerBackground.name(),
        t.lighterBackground.name());
}
