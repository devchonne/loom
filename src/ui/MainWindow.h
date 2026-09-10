#pragma once

#include "core/Settings.h"

#include <QMainWindow>
#include <QPair>
#include <QString>
#include <QVector>

class Buffer;
class BufferManager;
class CheatSheet;
class CrtWipe;
class Editor;
class FindBar;
class OutlineOverlay;
class PdfExportOverlay;
class RadioDirectory;
class RadioOverlay;
class RadioPlayer;
class StationLibrary;
class StatusBar;
class TabStrip;
class TabSwitcher;
class ThemeManager;
class ThemeSwitcher;
class Vault;
class VaultOverlay;
class VaultRegistry;
class VaultSidebar;
class WeatherSound;
class QSplitter;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(BufferManager* buffers, ThemeManager* themes, Settings settings,
               QWidget* parent = nullptr);
    ~MainWindow() override;

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
    void openPdfExport();
    bool exportPdf(const QString& templateId);
    void find(bool reverse);
    void wrapSelection(const QString& left, const QString& right);
    void duplicateLine();
    bool insertOrRefreshToc(const QString& arg);
    void alignTableAtCursor();
    bool insertTableSkeleton(const QString& arg);
    void renameTab();
    void toggleZen();
    void openSettings();
    void setThemeId(const QString& id);
    void cycleTheme(int delta);
    void layoutOverlays();
    bool dispatchSlash(const QString& name, const QString& arg);
    void openOutline();
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
    void openRadio();
    void syncRadioChrome();
    bool dispatchRadioSlash(const QString& arg);
    // Vault. Every one of these is a no-op unless a vault is open, which is what
    // keeps the feature free when it is switched off.
    void applyVaultSettings();
    void toggleVaultSidebar();
    void setVaultSidebarVisible(bool visible);
    void openVaultNotes();
    VaultOverlay* ensureVaultOverlay();
    void openVaultBacklinks();
    void openVaultSwitcher();
    void chooseVaultFolder();
    void setVaultRoot(const QString& root);
    // Resolves a wiki or file link and opens it, creating the note when a wiki
    // target does not exist yet. Returns false when nothing could be done.
    bool followDocumentLink(const QString& target, bool wiki);
    bool jumpBackAcrossFiles();
    void openPathAtLine(const QString& path, int line);
    void retargetBuffers(const QString& from, const QString& to);
    bool dispatchVaultSlash(const QString& arg);

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
    OutlineOverlay* outline_ = nullptr;
    ThemeSwitcher* themeSwitcher_ = nullptr;
    PdfExportOverlay* pdfExport_ = nullptr;
    CrtWipe* wipe_ = nullptr;
    QTimer* autosave_ = nullptr;
    QTimer* compareDiffTimer_ = nullptr;
    WeatherSound* weatherSound_ = nullptr;
    // Declared before radioOverlay_, which takes the first two in its
    // constructor: members initialise in declaration order.
    StationLibrary* stations_ = nullptr;
    RadioDirectory* directory_ = nullptr;
    RadioPlayer* radio_ = nullptr;
    RadioOverlay* radioOverlay_ = nullptr;
    // Declared before vaultSidebar_ / vaultOverlay_, which take it.
    Vault* vault_ = nullptr;
    VaultRegistry* vaults_ = nullptr;
    // Both are built lazily on first use: with the vault off, neither widget
    // exists, so there is nothing to hide and nothing to lay out.
    VaultSidebar* vaultSidebar_ = nullptr;
    VaultOverlay* vaultOverlay_ = nullptr;
    QSplitter* shell_ = nullptr;
    bool zen_ = false;
    bool shown_ = false;
    bool comparing_ = false;
    int compareArmed_ = -1;
    Buffer* compareLeftBuf_ = nullptr;
    Buffer* compareRightBuf_ = nullptr;
    // Cross-file jump history for Alt+Left, holding (path, cursor) pairs. Only
    // grows when a link actually crosses a file boundary.
    QVector<QPair<QString, int>> fileJumpStack_;
};
