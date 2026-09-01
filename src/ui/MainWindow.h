#pragma once

#include "core/Settings.h"

#include <QMainWindow>

class Buffer;
class BufferManager;
class CheatSheet;
class CrtWipe;
class Editor;
class FindBar;
class StatusBar;
class TabStrip;
class TabSwitcher;
class ThemeManager;
class ThemeSwitcher;
class WeatherSound;
class QSplitter;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(BufferManager* buffers, ThemeManager* themes, Settings settings,
               QWidget* parent = nullptr);

    void openPaths(const QStringList& paths);
    void newScratchTab();
    void persistSession();
    void restorePrefs(int zoom, bool zen, const QByteArray& geometry);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void wireShortcuts();
    void bindCurrentBuffer();
    void bindBufferToEditor(Buffer* buffer, Editor* editor);
    void captureViewState();
    void captureEditorState(Editor* editor, Buffer* buffer);
    void restoreViewState();
    void refreshChrome();
    void applyTheme();
    void applySettings(const Settings& settings);
    void saveCurrent(bool saveAs);
    void openFile();
    void find(bool reverse);
    void wrapSelection(const QString& left, const QString& right);
    void duplicateLine();
    void renameTab();
    void toggleZen();
    void openSettings();
    void setThemeId(const QString& id);
    void cycleTheme(int delta);
    void layoutOverlays();
    bool dispatchSlash(const QString& name, const QString& arg);
    int currentLine() const;
    int currentColumn() const;
    Editor* focusedEditor() const;
    void onTabClicked(int index);
    void onTabShiftClicked(int index);
    void enterCompare(int left, int right);
    void exitCompare(int keepIndex);
    void syncCompareAfterStructure();
    void scheduleCompareDiff();
    void refreshCompareDiff();
    void syncWeatherSound();

    BufferManager* buffers_ = nullptr;
    ThemeManager* themes_ = nullptr;
    Settings settings_;
    TabStrip* tabs_ = nullptr;
    QSplitter* splitter_ = nullptr;
    Editor* editor_ = nullptr;
    Editor* editorRight_ = nullptr;
    StatusBar* status_ = nullptr;
    FindBar* findBar_ = nullptr;
    CheatSheet* cheat_ = nullptr;
    TabSwitcher* switcher_ = nullptr;
    ThemeSwitcher* themeSwitcher_ = nullptr;
    CrtWipe* wipe_ = nullptr;
    QTimer* autosave_ = nullptr;
    QTimer* compareDiffTimer_ = nullptr;
    WeatherSound* weatherSound_ = nullptr;
    bool zen_ = false;
    bool shown_ = false;
    bool comparing_ = false;
    int compareArmed_ = -1;
    Buffer* compareLeftBuf_ = nullptr;
    Buffer* compareRightBuf_ = nullptr;
};
