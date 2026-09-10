#include "ui/MainWindow.h"

#include "core/AtomicFile.h"
#include "core/Buffer.h"
#include "core/BufferManager.h"
#include "core/LineDiff.h"
#include "core/Paths.h"
#include "core/PdfExport.h"
#include "core/RadioDirectory.h"
#include "core/RadioPlayer.h"
#include "core/SessionStore.h"
#include "core/SlashCommand.h"
#include "core/StationLibrary.h"
#include "core/Vault.h"
#include "core/VaultRegistry.h"
#include "markdown/DocumentOutline.h"
#include "markdown/TableFormat.h"
#include "theme/Fonts.h"
#include "theme/ThemeManager.h"
#include "ui/CheatSheet.h"
#include "ui/CrtWipe.h"
#include "ui/Editor.h"
#include "ui/FindBar.h"
#include "ui/OutlineOverlay.h"
#include "ui/PdfExportOverlay.h"
#include "ui/RadioOverlay.h"
#include "ui/SettingsDialog.h"
#include "ui/StatusBar.h"
#include "ui/TabStrip.h"
#include "ui/TabSwitcher.h"
#include "ui/ThemedDialogs.h"
#include "ui/ThemeSwitcher.h"
#include "ui/VaultOverlay.h"
#include "ui/VaultSidebar.h"
#include "ui/WeatherSound.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeySequence>
#include <QPair>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(BufferManager* buffers, ThemeManager* themes, Settings settings, QWidget* parent)
    : QMainWindow(parent)
    , buffers_(buffers)
    , themes_(themes)
    , settings_(std::move(settings))
    , tabs_(new TabStrip(buffers_, this))
    , splitter_(new QSplitter(Qt::Horizontal, this))
    , editor_(new Editor(this))
    , editorRight_(new Editor(this))
    , status_(new StatusBar(this))
    , findBar_(new FindBar(this))
    , cheat_(new CheatSheet(this))
    , switcher_(new TabSwitcher(buffers_, this))
    , outline_(new OutlineOverlay(this))
    , themeSwitcher_(new ThemeSwitcher(this))
    , pdfExport_(new PdfExportOverlay(this))
    , wipe_(new CrtWipe(this))
    , autosave_(new QTimer(this))
    , compareDiffTimer_(new QTimer(this))
    , weatherSound_(new WeatherSound(this))
    , stations_(new StationLibrary(this))
    , directory_(new RadioDirectory(this))
    , radio_(new RadioPlayer(this))
    , radioOverlay_(new RadioOverlay(stations_, directory_, this))
    , vault_(new Vault(this))
    , vaults_(new VaultRegistry(this))
    , zen_(settings_.zenByDefault) {
    setWindowTitle(QStringLiteral("loom"));
    resize(960, 700);

    splitter_->setChildrenCollapsible(false);
    splitter_->addWidget(editor_);
    splitter_->addWidget(editorRight_);
    editorRight_->hide();
    editor_->installEventFilter(this);
    editorRight_->installEventFilter(this);

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("loomRoot"));
    // The editor column: tabs, the compare splitter, find bar and footer. This
    // is the whole window when there is no vault sidebar.
    auto* column = new QWidget(root);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);
    columnLayout->addWidget(tabs_);
    columnLayout->addWidget(splitter_, 1);
    columnLayout->addWidget(findBar_);
    columnLayout->addWidget(status_);

    // An outer splitter so the sidebar can be resized. splitter_ cannot be
    // reused for this: it belongs to compare mode.
    shell_ = new QSplitter(Qt::Horizontal, root);
    shell_->setChildrenCollapsible(false);
    shell_->addWidget(column);

    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(shell_, 1);
    setCentralWidget(root);

    cheat_->setParent(root);
    switcher_->setParent(root);
    outline_->setParent(root);
    themeSwitcher_->setParent(root);
    pdfExport_->setParent(root);
    radioOverlay_->setParent(root);
    wipe_->setParent(root);

    autosave_->setSingleShot(true);
    autosave_->setInterval(600);
    connect(autosave_, &QTimer::timeout, this, &MainWindow::persistSession);
    compareDiffTimer_->setSingleShot(true);
    compareDiffTimer_->setInterval(80);
    connect(compareDiffTimer_, &QTimer::timeout, this, &MainWindow::refreshCompareDiff);

    connect(buffers_, &BufferManager::structureChanged, this, [this]() {
        syncCompareAfterStructure();
        tabs_->update();
        refreshChrome();
    });
    connect(buffers_, &BufferManager::aboutToChangeCurrent, this, &MainWindow::captureViewState);
    connect(buffers_, &BufferManager::currentChanged, this, [this](int) { bindCurrentBuffer(); });
    connect(buffers_, &BufferManager::contentsChanged, this, [this]() {
        autosave_->start();
        refreshChrome();
        if (comparing_) {
            scheduleCompareDiff();
        }
    });
    connect(tabs_, &TabStrip::tabClicked, this, &MainWindow::onTabClicked);
    connect(tabs_, &TabStrip::tabShiftClicked, this, &MainWindow::onTabShiftClicked);
    connect(tabs_, &TabStrip::tabCloseRequested, buffers_, &BufferManager::closeAt);
    connect(tabs_, &TabStrip::tabMoved, buffers_, &BufferManager::moveTab);
    connect(tabs_, &TabStrip::tabRenameRequested, this, &MainWindow::renameTab);
    connect(themes_, &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
    connect(editor_, &Editor::cursorInfoChanged, this, &MainWindow::refreshChrome);
    connect(editorRight_, &Editor::cursorInfoChanged, this, &MainWindow::refreshChrome);
    connect(editor_, &Editor::zoomChanged, this, [this](int percent) {
        editorRight_->setZoom(percent);
        refreshChrome();
        persistSession();
    });
    connect(editorRight_, &Editor::zoomChanged, this, [this](int percent) { editor_->setZoom(percent); });
    connect(findBar_, &FindBar::findNext, this, [this]() { find(false); });
    connect(findBar_, &FindBar::findPrev, this, [this]() { find(true); });
    connect(findBar_, &FindBar::closed, this, [this]() { focusedEditor()->setFocus(); });
    connect(switcher_, &TabSwitcher::chosen, this, &MainWindow::onTabClicked);
    connect(cheat_, &CheatSheet::closed, this, [this]() { focusedEditor()->setFocus(); });
    connect(outline_, &OutlineOverlay::chosen, this, [this](int blockNumber) {
        Editor* editor = focusedEditor();
        QTextDocument* doc = editor->document();
        if (!doc) {
            return;
        }
        const QTextBlock block = doc->findBlockByNumber(blockNumber);
        if (!block.isValid()) {
            return;
        }
        QTextCursor cursor(block);
        editor->setTextCursor(cursor);
        editor->ensureCursorVisible();
        editor->setFocus();
    });
    connect(themeSwitcher_, &ThemeSwitcher::chosen, this, &MainWindow::setThemeId);
    connect(pdfExport_, &PdfExportOverlay::chosen, this,
            [this](const QString& id) { exportPdf(id); });
    connect(radioOverlay_, &RadioOverlay::playRequested, this, [this](const QString& id) {
        if (const RadioStation* station = stations_->byId(id)) {
            radio_->play(*station);
        }
    });
    connect(radioOverlay_, &RadioOverlay::stopRequested, this, [this]() { radio_->stop(); });
    connect(radioOverlay_, &RadioOverlay::miniPlayerToggled, this, [this](bool enabled) {
        settings_.radioMiniPlayer = enabled;
        settings_.save();
        syncRadioChrome();
    });
    connect(radio_, &RadioPlayer::stateChanged, this, [this](RadioState) { syncRadioChrome(); });
    connect(radio_, &RadioPlayer::stationChanged, this, [this](const QString& id) {
        settings_.radioLastStation = id;
        settings_.save();
        radioOverlay_->setPlayingStation(id);
    });
    connect(radio_, &RadioPlayer::started, this,
            [this](const QString& id) { stations_->markPlayed(id); });
    connect(radio_, &RadioPlayer::message, this,
            [this](const QString& text) { radioOverlay_->setStatusMessage(text); });
    connect(stations_, &StationLibrary::changed, this, [this]() { syncRadioChrome(); });
    connect(status_, &StatusBar::radioToggleClicked, this, [this]() {
        if (!radio_->isActive() && !settings_.radioLastStation.isEmpty()) {
            // Nothing loaded yet: fall back to whatever played last.
            if (const RadioStation* station = stations_->byId(settings_.radioLastStation)) {
                radio_->play(*station);
                return;
            }
        }
        radio_->toggle();
    });
    connect(status_, &StatusBar::radioStopClicked, this, [this]() { radio_->stop(); });
    connect(status_, &StatusBar::radioOpenClicked, this, [this]() { openRadio(); });
    connect(editor_, &Editor::slashCommand, this,
            [this](const QString& name, const QString& arg, bool* accepted) {
                if (accepted) {
                    *accepted = dispatchSlash(name, arg);
                }
            });
    connect(editorRight_, &Editor::slashCommand, this,
            [this](const QString& name, const QString& arg, bool* accepted) {
                if (accepted) {
                    *accepted = dispatchSlash(name, arg);
                }
            });
    for (Editor* editor : {editor_, editorRight_}) {
        connect(editor, &Editor::documentLinkActivated, this,
                [this](const QString& target, bool wiki, bool* handled) {
                    if (handled) {
                        *handled = followDocumentLink(target, wiki);
                    }
                });
        connect(editor, &Editor::jumpBackExhausted, this, [this](bool* handled) {
            if (handled) {
                *handled = jumpBackAcrossFiles();
            }
        });
    }
    connect(vault_, &Vault::indexChanged, this, [this]() {
        if (vaultSidebar_ && vaultSidebar_->isVisible()) {
            Buffer* buffer = buffers_->current();
            vaultSidebar_->syncCurrentPath(buffer ? buffer->path() : QString());
        }
    });

    wireShortcuts();
    applySettings(settings_);
    // The library has to exist before the footer can decide whether to show the
    // mini player at all.
    stations_->load();
    radioOverlay_->setPlayingStation(settings_.radioLastStation);
    syncRadioChrome();
    // Only touches vaults.json when the feature is actually on.
    applyVaultSettings();
    if (buffers_->count() == 0) {
        buffers_->createScratch();
    } else {
        bindCurrentBuffer();
    }
    editor_->setZen(zen_);
    editorRight_->setZen(zen_);
    tabs_->setVisible(!zen_);
    status_->setVisible(!zen_);
    persistSession();
}

MainWindow::~MainWindow() {
    // The editors hold highlighters attached to documents the BufferManager
    // owns, and QSyntaxHighlighter re-enters its document while detaching. Let
    // go of the documents first so that teardown cannot run buffer signal
    // handlers against half-destroyed widgets.
    editor_->unbindDocument();
    editorRight_->unbindDocument();
    // Drops the vault's watcher and the sidebar's file-gathering worker before
    // the event loop is gone.
    vault_->setRoot(QString());
    delete vaultSidebar_;
    vaultSidebar_ = nullptr;
}

void MainWindow::wireShortcuts() {
    auto add = [this](const QKeySequence& seq, auto slot) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, slot);
        return sc;
    };

    add(QKeySequence(QStringLiteral("Ctrl+S")), [this]() { saveCurrent(false); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this]() { saveCurrent(true); });
    add(QKeySequence(QStringLiteral("Ctrl+O")), [this]() { openFile(); });
    add(QKeySequence(QStringLiteral("Ctrl+N")), [this]() { newScratchTab(); });
    add(QKeySequence(QStringLiteral("Ctrl+W")), [this]() {
        captureViewState();
        buffers_->closeAt(buffers_->currentIndex());
        persistSession();
    });
    add(QKeySequence(QStringLiteral("Ctrl+Tab")), [this]() {
        if (buffers_->count() == 0) {
            return;
        }
        buffers_->setCurrentIndex((buffers_->currentIndex() + 1) % buffers_->count());
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), [this]() {
        if (buffers_->count() == 0) {
            return;
        }
        const int n = buffers_->count();
        buffers_->setCurrentIndex((buffers_->currentIndex() + n - 1) % n);
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+[")), [this]() {
        const int i = buffers_->currentIndex();
        if (i > 0) {
            buffers_->moveTab(i, i - 1);
        }
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+]")), [this]() {
        const int i = buffers_->currentIndex();
        if (i >= 0 && i + 1 < buffers_->count()) {
            buffers_->moveTab(i, i + 1);
        }
    });
    add(QKeySequence(QStringLiteral("Ctrl+R")), [this]() { renameTab(); });
    add(QKeySequence::Undo, [this]() { focusedEditor()->undoEdit(); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+Z")), [this]() { focusedEditor()->redoEdit(); });
    add(QKeySequence::Redo, [this]() { focusedEditor()->redoEdit(); });
    add(QKeySequence(QStringLiteral("Ctrl+F")), [this]() {
        const QString seed = focusedEditor()->textCursor().selectedText();
        findBar_->open(seed);
    });
    add(QKeySequence(QStringLiteral("Ctrl+G")), [this]() { find(false); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+G")), [this]() { find(true); });
    add(QKeySequence(QStringLiteral("Ctrl+D")), [this]() { duplicateLine(); });
    add(QKeySequence(QStringLiteral("Ctrl+B")), [this]() { wrapSelection(QStringLiteral("**"), QStringLiteral("**")); });
    add(QKeySequence(QStringLiteral("Ctrl+I")), [this]() { wrapSelection(QStringLiteral("*"), QStringLiteral("*")); });
    add(QKeySequence(QStringLiteral("Ctrl+L")), [this]() {
        wrapSelection(QStringLiteral("["), QStringLiteral("](url)"));
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+\\")), [this]() { alignTableAtCursor(); });
    add(QKeySequence(QStringLiteral("Ctrl+=")), [this]() { editor_->zoomBy(10); });
    add(QKeySequence(QStringLiteral("Ctrl++")), [this]() { editor_->zoomBy(10); });
    add(QKeySequence(QStringLiteral("Ctrl+-")), [this]() { editor_->zoomBy(-10); });
    add(QKeySequence(QStringLiteral("Ctrl+0")), [this]() { editor_->resetZoom(); });
    add(QKeySequence(QStringLiteral("Ctrl+M")), [this]() {
        const bool on = !editor_->markdownEnabled();
        editor_->setMarkdownEnabled(on);
        editorRight_->setMarkdownEnabled(on);
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+F")), [this]() { toggleZen(); });
    add(QKeySequence(QStringLiteral("Ctrl+Alt+W")), [this]() {
        editor_->setWeatherMode(WeatherMode::Rain);
        editorRight_->setWeatherMode(editor_->weatherMode(), false);
        syncWeatherSound();
    });
    add(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+W")), [this]() {
        editor_->setWeatherMode(WeatherMode::Storm);
        editorRight_->setWeatherMode(editor_->weatherMode(), false);
        syncWeatherSound();
    });
    add(QKeySequence(QStringLiteral("F11")), [this]() {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });
    add(QKeySequence(QStringLiteral("Ctrl+,")), [this]() { openSettings(); });
    add(QKeySequence(QStringLiteral("Ctrl+T")), [this]() {
        themeSwitcher_->setGeometry(centralWidget()->rect());
        themeSwitcher_->open(settings_.themeSource);
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+T")), [this]() { cycleTheme(1); });
    // Deliberately undiscoverable: pdf export has no chrome, only this key,
    // /pdf, and a row in the Ctrl+K cheat sheet.
    add(QKeySequence(QStringLiteral("Ctrl+Shift+P")), [this]() { openPdfExport(); });
    add(QKeySequence(QStringLiteral("Ctrl+Alt+R")), [this]() { openRadio(); });
    add(QKeySequence(QStringLiteral("Ctrl+Alt+P")), [this]() {
        if (!radio_->isActive() && !settings_.radioLastStation.isEmpty()) {
            if (const RadioStation* station = stations_->byId(settings_.radioLastStation)) {
                radio_->play(*station);
                return;
            }
        }
        radio_->toggle();
    });
    add(QKeySequence(QStringLiteral("Ctrl+Alt+S")), [this]() { radio_->stop(); });
    add(QKeySequence(QStringLiteral("Ctrl+K")), [this]() {
        cheat_->setGeometry(centralWidget()->rect());
        cheat_->toggle();
    });
    add(QKeySequence(QStringLiteral("Ctrl+P")), [this]() {
        switcher_->setGeometry(centralWidget()->rect());
        switcher_->open();
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+O")), [this]() { openOutline(); });
    // Vault. Silent no-ops with the feature off, and the sidebar toggle leaves
    // no trace in the chrome when hidden.
    add(QKeySequence(QStringLiteral("Ctrl+E")), [this]() { toggleVaultSidebar(); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+E")), [this]() { openVaultNotes(); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+B")), [this]() { openVaultBacklinks(); });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this]() { openVaultSwitcher(); });
    add(QKeySequence(QStringLiteral("Ctrl+Q")), [this]() { close(); });
    add(QKeySequence(Qt::Key_Escape), [this]() {
        if (cheat_->isVisible()) {
            cheat_->dismiss();
        } else if (switcher_->isVisible()) {
            switcher_->hide();
        } else if (outline_->isVisible()) {
            outline_->hide();
        } else if (themeSwitcher_->isVisible()) {
            themeSwitcher_->hide();
        } else if (pdfExport_->isVisible()) {
            pdfExport_->hide();
        } else if (radioOverlay_->isVisible()) {
            radioOverlay_->dismiss();
        } else if (vaultOverlay_ && vaultOverlay_->isVisible()) {
            vaultOverlay_->hide();
        } else if (findBar_->isVisible()) {
            findBar_->hide();
            focusedEditor()->setFocus();
        }
    });

    for (int i = 1; i <= 9; ++i) {
        add(QKeySequence(QStringLiteral("Alt+%1").arg(i)), [this, i]() {
            if (i - 1 < buffers_->count()) {
                buffers_->setCurrentIndex(i - 1);
            }
        });
    }
}

void MainWindow::bindCurrentBuffer() {
    Buffer* buffer = buffers_->current();
    if (!buffer) {
        return;
    }
    if (comparing_) {
        if (buffer == compareRightBuf_) {
            editorRight_->setFocus();
        } else if (buffer == compareLeftBuf_) {
            editor_->setFocus();
        } else {
            exitCompare(buffers_->currentIndex());
            return;
        }
        refreshChrome();
        tabs_->update();
        return;
    }
    bindBufferToEditor(buffer, editor_);
    refreshChrome();
    editor_->setFocus();
    tabs_->update();
    if (vaultSidebar_ && vaultSidebar_->isVisible()) {
        vaultSidebar_->syncCurrentPath(buffer->path());
    }
}

void MainWindow::bindBufferToEditor(Buffer* buffer, Editor* editor) {
    if (!buffer || !editor) {
        return;
    }
    editor->bindDocument(buffer->document(), false);
    editor->setResourceDir(buffer->path().isEmpty() ? QString()
                                                    : QFileInfo(buffer->path()).absolutePath());
    QTextCursor cursor(buffer->document());
    cursor.setPosition(qBound(0, buffer->cursor(), buffer->document()->characterCount() - 1));
    editor->setTextCursor(cursor);
    editor->verticalScrollBar()->setValue(buffer->scroll());
}

void MainWindow::restorePrefs(int zoom, bool zen, const QByteArray& geometry) {
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    editor_->setZoom(zoom);
    editorRight_->setZoom(zoom);
    zen_ = zen;
    tabs_->setVisible(!zen_);
    status_->setVisible(!zen_);
    editor_->setZen(zen_);
    editorRight_->setZen(zen_);
    // A restored zen session must not bring the tree back with it.
    setVaultSidebarVisible(settings_.vaultSidebarVisible);
}

void MainWindow::captureViewState() {
    if (comparing_) {
        captureEditorState(editor_, compareLeftBuf_);
        captureEditorState(editorRight_, compareRightBuf_);
        return;
    }
    captureEditorState(editor_, buffers_->current());
}

void MainWindow::captureEditorState(Editor* editor, Buffer* buffer) {
    if (!editor || !buffer) {
        return;
    }
    buffer->setCursor(editor->textCursor().position());
    buffer->setScroll(editor->verticalScrollBar()->value());
}

void MainWindow::restoreViewState() {
    bindBufferToEditor(buffers_->current(), editor_);
}

void MainWindow::refreshChrome() {
    Buffer* buffer = buffers_->current();
    if (!buffer) {
        return;
    }
    const QString title = buffer->isUnnamed() ? buffer->title() : QFileInfo(buffer->path()).fileName();
    setWindowTitle(title + QStringLiteral(" — loom"));
    status_->setInfo(title, focusedEditor()->wordCount(), currentLine(), currentColumn(), buffer->isDirty(),
                     editor_->zoom(), Palettes::displayName(settings_.themeSource));
}

int MainWindow::currentLine() const {
    return focusedEditor()->textCursor().blockNumber() + 1;
}

int MainWindow::currentColumn() const {
    return focusedEditor()->textCursor().positionInBlock() + 1;
}

Editor* MainWindow::focusedEditor() const {
    if (comparing_ && editorRight_->hasFocus()) {
        return editorRight_;
    }
    return editor_;
}

void MainWindow::applyTheme() {
    const Theme& theme = themes_->theme();
    const QString sheet = themes_->styleSheet();
    qApp->setPalette(themes_->palette());
    qApp->setStyleSheet(sheet);
    setStyleSheet(sheet);
    editor_->setTheme(theme);
    editorRight_->setTheme(theme);
    tabs_->setTheme(theme);
    status_->setTheme(theme);
    findBar_->setTheme(theme);
    cheat_->setTheme(theme);
    switcher_->setTheme(theme);
    outline_->setTheme(theme);
    themeSwitcher_->setTheme(theme);
    pdfExport_->setTheme(theme);
    radioOverlay_->setTheme(theme);
    wipe_->setTheme(theme);
    if (vaultSidebar_) {
        vaultSidebar_->setTheme(theme);
    }
    if (vaultOverlay_) {
        vaultOverlay_->setTheme(theme);
    }
    if (comparing_) {
        refreshCompareDiff();
    }
}

void MainWindow::applySettings(const Settings& settings) {
    settings_ = settings;
    themes_->apply(settings_);
    const QFont chrome = Fonts::chrome(settings_, 10);
    tabs_->setChromeFont(chrome);
    status_->setChromeFont(chrome);
    findBar_->setChromeFont(chrome);
    cheat_->setChromeFont(chrome);
    switcher_->setChromeFont(chrome);
    outline_->setChromeFont(chrome);
    themeSwitcher_->setChromeFont(chrome);
    pdfExport_->setChromeFont(chrome);
    radioOverlay_->setChromeFont(chrome);
    if (vaultSidebar_) {
        vaultSidebar_->setChromeFont(chrome);
    }
    if (vaultOverlay_) {
        vaultOverlay_->setChromeFont(chrome);
    }
    editor_->applySettings(settings_);
    editorRight_->applySettings(settings_);
    radio_->setVolume(settings_.radioVolume);
    radioOverlay_->setMiniPlayerEnabled(settings_.radioMiniPlayer);
    syncRadioChrome();
    applyTheme();
}

void MainWindow::saveCurrent(bool saveAs) {
    Buffer* buffer = buffers_->current();
    if (!buffer) {
        return;
    }
    QString path = buffer->path();
    if (saveAs || path.isEmpty()) {
        QDir().mkpath(settings_.resolvedNotesDirectory());
        path = ThemedDialogs::getSaveFileName(this, QStringLiteral("save"),
                                              path.isEmpty() ? settings_.resolvedNotesDirectory() + QLatin1Char('/')
                                                                   + buffer->title() + QStringLiteral(".md")
                                                             : path,
                                              QStringLiteral("Markdown (*.md);;Text (*.txt);;All (*)"));
        if (path.isEmpty()) {
            return;
        }
        buffer->setPath(path);
        buffer->setTitle(QFileInfo(path).fileName());
    }
    QString err;
    if (!AtomicFile::write(path, buffer->text().toUtf8(), &err)) {
        return;
    }
    buffer->markClean();
    persistSession();
    refreshChrome();
}

void MainWindow::openFile() {
    const QString path = ThemedDialogs::getOpenFileName(this, QStringLiteral("open"),
                                                       settings_.resolvedNotesDirectory(),
                                                       QStringLiteral("Markdown (*.md);;Text (*.txt);;All (*)"));
    if (path.isEmpty()) {
        return;
    }
    openPaths({path});
}

void MainWindow::openPdfExport() {
    pdfExport_->setGeometry(centralWidget()->rect());
    pdfExport_->open(settings_.pdfTemplate);
}

bool MainWindow::exportPdf(const QString& templateId) {
    Buffer* buffer = buffers_->current();
    if (!buffer) {
        return false;
    }
    const QString id = PdfTemplates::normalize(templateId);
    const QString title = buffer->isUnnamed() ? buffer->title()
                                             : QFileInfo(buffer->path()).completeBaseName();

    QString suggested;
    if (buffer->path().isEmpty()) {
        QDir().mkpath(settings_.resolvedNotesDirectory());
        suggested = settings_.resolvedNotesDirectory() + QLatin1Char('/') + buffer->title()
                    + QStringLiteral(".pdf");
    } else {
        const QFileInfo info(buffer->path());
        suggested = info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
                    + QStringLiteral(".pdf");
    }

    QString path = ThemedDialogs::getSaveFileName(this, QStringLiteral("export pdf"), suggested,
                                                  QStringLiteral("PDF (*.pdf);;All (*)"));
    if (path.isEmpty()) {
        focusedEditor()->setFocus();
        return false;
    }
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".pdf");
    }

    PdfExportRequest request;
    request.markdown = buffer->text();
    request.title = title;
    request.templateId = id;
    request.baseDir = buffer->path().isEmpty() ? Paths::mediaDir()
                                               : QFileInfo(buffer->path()).absolutePath();
    request.theme = themes_->theme();
    request.settings = settings_;

    QString err;
    const bool ok = PdfExport::write(path, request, &err);
    if (ok && settings_.pdfTemplate != id) {
        settings_.pdfTemplate = id;
        settings_.save();
    }
    focusedEditor()->setFocus();
    return ok;
}

void MainWindow::openPaths(const QStringList& paths) {
    for (const QString& path : paths) {
        QFile file(path);
        QString text;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(file.readAll());
        }
        captureViewState();
        buffers_->openPath(QFileInfo(path).absoluteFilePath(), text);
    }
    persistSession();
}

void MainWindow::newScratchTab() {
    captureViewState();
    buffers_->createScratch();
    persistSession();
}

void MainWindow::find(bool reverse) {
    const QString query = findBar_->query();
    if (query.isEmpty()) {
        findBar_->open();
        return;
    }
    Editor* editor = focusedEditor();
    const QTextDocument::FindFlags flags = reverse ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
    if (!editor->find(query, flags)) {
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(reverse ? QTextCursor::End : QTextCursor::Start);
        editor->setTextCursor(cursor);
        editor->find(query, flags);
    }
}

void MainWindow::wrapSelection(const QString& left, const QString& right) {
    Editor* editor = focusedEditor();
    QTextCursor cursor = editor->textCursor();
    const QString selected = cursor.hasSelection() ? cursor.selectedText() : QString();
    cursor.insertText(left + selected + right);
    if (selected.isEmpty()) {
        cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, right.size());
        editor->setTextCursor(cursor);
    }
}

void MainWindow::duplicateLine() {
    Editor* editor = focusedEditor();
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QString line = cursor.selectedText();
    cursor.clearSelection();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText(QLatin1Char('\n') + line);
}

void MainWindow::openOutline() {
    Editor* editor = focusedEditor();
    QTextDocument* doc = editor->document();
    if (!doc) {
        return;
    }
    const auto entries = DocumentOutline::build(doc->toPlainText().split(QLatin1Char('\n')));
    outline_->setGeometry(centralWidget()->rect());
    outline_->open(entries);
}

bool MainWindow::insertOrRefreshToc(const QString& arg) {
    Editor* editor = focusedEditor();
    QTextDocument* doc = editor->document();
    if (!doc) {
        return false;
    }

    int maxLevel = 6;
    if (!arg.isEmpty()) {
        bool ok = false;
        const int level = arg.toInt(&ok);
        if (!ok || level < 1 || level > 6) {
            return false;
        }
        maxLevel = level;
    }

    const QString fullText = doc->toPlainText();
    const auto entries = DocumentOutline::build(fullText.split(QLatin1Char('\n')));
    const QString body = DocumentOutline::tocMarkdown(entries, maxLevel);

    static const QString openMarker = QStringLiteral("<!-- toc -->");
    static const QString closeMarker = QStringLiteral("<!-- /toc -->");
    const int openIdx = fullText.indexOf(openMarker);
    const int closeIdx = openIdx >= 0 ? fullText.indexOf(closeMarker, openIdx + openMarker.size()) : -1;

    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    if (openIdx >= 0 && closeIdx > openIdx) {
        cursor.setPosition(openIdx + openMarker.size());
        cursor.setPosition(closeIdx, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.insertText(QLatin1Char('\n') + body + QLatin1Char('\n'));
    } else {
        cursor = editor->textCursor();
        if (cursor.positionInBlock() > 0 || !cursor.block().text().isEmpty()) {
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
        }
        cursor.insertText(openMarker + QLatin1Char('\n') + body + QLatin1Char('\n') + closeMarker);
        cursor.insertBlock();
    }
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    return true;
}

void MainWindow::alignTableAtCursor() {
    // Delegates to the editor so run detection, alignment and caret restoration
    // all use one implementation instead of a divergent copy.
    focusedEditor()->alignTableAtCursor();
}

bool MainWindow::insertTableSkeleton(const QString& arg) {
    int cols = 3;
    int rows = 2;
    static const QRegularExpression reSize(QStringLiteral(R"(^(\d+)x(\d+)$)"), QRegularExpression::CaseInsensitiveOption);
    const QString trimmed = arg.trimmed();
    if (!trimmed.isEmpty()) {
        const auto m = reSize.match(trimmed);
        if (!m.hasMatch()) {
            return false;
        }
        cols = qBound(1, m.captured(1).toInt(), 32);
        rows = qBound(0, m.captured(2).toInt(), 200);
    }

    Editor* editor = focusedEditor();
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    if (cursor.positionInBlock() > 0 || !cursor.block().text().isEmpty()) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
    }
    const int headerBlock = cursor.blockNumber();
    cursor.insertText(TableFormat::skeleton(cols, rows));
    // Trailing block so there is somewhere to continue writing below the table.
    cursor.insertBlock();
    cursor.endEditBlock();
    // Leave the caret in the first header cell, ready to type.
    if (!editor->focusTableCell(headerBlock, 0)) {
        editor->setTextCursor(cursor);
    }
    return true;
}

void MainWindow::renameTab() {
    Buffer* buffer = buffers_->current();
    if (!buffer) {
        return;
    }
    bool ok = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("rename"), QStringLiteral("tab name"),
                                                QLineEdit::Normal, buffer->title(), &ok);
    if (ok && !title.trimmed().isEmpty()) {
        buffer->setTitle(title.trimmed());
        persistSession();
    }
}

void MainWindow::toggleZen() {
    zen_ = !zen_;
    tabs_->setVisible(!zen_);
    status_->setVisible(!zen_);
    editor_->setZen(zen_);
    editorRight_->setZen(zen_);
    // Zen means nothing but the text, so the sidebar goes with the tabs and the
    // footer. The setting is untouched, so leaving zen brings it back.
    setVaultSidebarVisible(settings_.vaultSidebarVisible);
    persistSession();
}

void MainWindow::openSettings() {
    SettingsDialog dialog(settings_, this);
    if (dialog.exec() == QDialog::Accepted) {
        applySettings(dialog.result());
        applyVaultSettings();
        settings_.save();
    }
}

void MainWindow::setThemeId(const QString& id) {
    if (id.isEmpty()) {
        return;
    }
    settings_.themeSource = id;
    applySettings(settings_);
    settings_.save();
    focusedEditor()->setFocus();
}

void MainWindow::cycleTheme(int delta) {
    setThemeId(Palettes::cycle(settings_.themeSource, delta));
}

void MainWindow::onTabClicked(int index) {
    if (index < 0 || index >= buffers_->count()) {
        return;
    }
    if (comparing_) {
        Buffer* buf = buffers_->at(index);
        if (buf == compareLeftBuf_ || buf == compareRightBuf_) {
            buffers_->setCurrentIndex(index);
            return;
        }
        exitCompare(index);
        return;
    }
    buffers_->setCurrentIndex(index);
}

void MainWindow::onTabShiftClicked(int index) {
    if (index < 0 || index >= buffers_->count()) {
        return;
    }
    if (comparing_) {
        Buffer* buf = buffers_->at(index);
        if (buf == compareLeftBuf_ || buf == compareRightBuf_) {
            exitCompare(index);
            return;
        }
        exitCompare(index);
        compareArmed_ = index;
        tabs_->setArmedIndex(index);
        return;
    }
    if (compareArmed_ < 0) {
        compareArmed_ = index;
        tabs_->setArmedIndex(index);
        return;
    }
    if (compareArmed_ == index) {
        compareArmed_ = -1;
        tabs_->setArmedIndex(-1);
        return;
    }
    enterCompare(compareArmed_, index);
}

void MainWindow::enterCompare(int left, int right) {
    Buffer* leftBuf = buffers_->at(left);
    Buffer* rightBuf = buffers_->at(right);
    if (!leftBuf || !rightBuf || leftBuf == rightBuf) {
        return;
    }
    captureViewState();
    comparing_ = true;
    compareArmed_ = -1;
    compareLeftBuf_ = leftBuf;
    compareRightBuf_ = rightBuf;
    tabs_->setArmedIndex(-1);
    tabs_->setComparePair(left, right);
    editorRight_->setZoom(editor_->zoom());
    editorRight_->setMarkdownEnabled(editor_->markdownEnabled());
    editorRight_->setWeatherMode(editor_->weatherMode(), false);
    editorRight_->setZen(zen_);
    bindBufferToEditor(leftBuf, editor_);
    bindBufferToEditor(rightBuf, editorRight_);
    editorRight_->show();
    splitter_->setSizes({width() / 2, width() / 2});
    scheduleCompareDiff();
    buffers_->setCurrentIndex(right);
    editorRight_->setFocus();
}

void MainWindow::exitCompare(int keepIndex) {
    if (!comparing_) {
        compareArmed_ = -1;
        tabs_->setArmedIndex(-1);
        if (keepIndex >= 0) {
            buffers_->setCurrentIndex(keepIndex);
        }
        return;
    }
    captureViewState();
    comparing_ = false;
    compareLeftBuf_ = nullptr;
    compareRightBuf_ = nullptr;
    compareArmed_ = -1;
    editor_->setDiffHighlights({});
    editorRight_->setDiffHighlights({});
    editorRight_->unbindDocument();
    editorRight_->hide();
    tabs_->setComparePair(-1, -1);
    tabs_->setArmedIndex(-1);
    if (keepIndex >= 0 && keepIndex < buffers_->count() && keepIndex != buffers_->currentIndex()) {
        buffers_->setCurrentIndex(keepIndex);
    } else {
        bindCurrentBuffer();
    }
}

void MainWindow::syncCompareAfterStructure() {
    if (compareArmed_ >= buffers_->count()) {
        compareArmed_ = -1;
        tabs_->setArmedIndex(-1);
    }
    if (!comparing_) {
        return;
    }
    const int left = buffers_->indexOf(compareLeftBuf_);
    const int right = buffers_->indexOf(compareRightBuf_);
    if (left < 0 || right < 0) {
        const int keep = left >= 0 ? left : (right >= 0 ? right : buffers_->currentIndex());
        exitCompare(keep);
        return;
    }
    tabs_->setComparePair(left, right);
}

void MainWindow::syncWeatherSound() {
    weatherSound_->setWeather(editor_->weatherMode());
}

void MainWindow::openRadio() {
    radioOverlay_->setGeometry(centralWidget()->rect());
    radioOverlay_->open();
}

void MainWindow::syncRadioChrome() {
    // The mini player only exists once there is something to play, and can be
    // switched off entirely from the radio dialog.
    const bool visible = settings_.radioMiniPlayer && !stations_->isEmpty();
    status_->setRadio(visible, radio_->state());
}

void MainWindow::scheduleCompareDiff() {
    if (comparing_) {
        compareDiffTimer_->start();
    }
}

void MainWindow::refreshCompareDiff() {
    if (!comparing_ || !compareLeftBuf_ || !compareRightBuf_) {
        return;
    }
    QStringList leftLines;
    QStringList rightLines;
    for (QTextBlock block = compareLeftBuf_->document()->begin(); block.isValid(); block = block.next()) {
        leftLines.append(block.text());
    }
    for (QTextBlock block = compareRightBuf_->document()->begin(); block.isValid(); block = block.next()) {
        rightLines.append(block.text());
    }
    const LineDiffResult diff = diffLines(leftLines, rightLines);
    const Theme& theme = themes_->theme();
    const int alpha = theme.dark ? 70 : 50;
    auto tint = [alpha](QColor color) {
        color.setAlpha(alpha);
        return color;
    };
    const QColor del = tint(theme.red);
    const QColor ins = tint(theme.green);
    const QColor chg = tint(theme.yellow);
    QVector<QPair<int, QColor>> leftTints;
    QVector<QPair<int, QColor>> rightTints;
    for (int i = 0; i < diff.left.size(); ++i) {
        if (diff.left[i] == LineMark::Delete) {
            leftTints.append({i, del});
        } else if (diff.left[i] == LineMark::Change) {
            leftTints.append({i, chg});
        }
    }
    for (int i = 0; i < diff.right.size(); ++i) {
        if (diff.right[i] == LineMark::Insert) {
            rightTints.append({i, ins});
        } else if (diff.right[i] == LineMark::Change) {
            rightTints.append({i, chg});
        }
    }
    editor_->setDiffHighlights(leftTints);
    editorRight_->setDiffHighlights(rightTints);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (comparing_ && event->type() == QEvent::FocusIn) {
        if (watched == editorRight_ && compareRightBuf_) {
            const int i = buffers_->indexOf(compareRightBuf_);
            if (i >= 0 && i != buffers_->currentIndex()) {
                buffers_->setCurrentIndex(i);
            }
        } else if (watched == editor_ && compareLeftBuf_) {
            const int i = buffers_->indexOf(compareLeftBuf_);
            if (i >= 0 && i != buffers_->currentIndex()) {
                buffers_->setCurrentIndex(i);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---- vault ------------------------------------------------------------------

void MainWindow::applyVaultSettings() {
    const QString root = settings_.activeVaultRoot();
    if (root.isEmpty()) {
        // Switched off: drop the index and the watcher, and take the sidebar out
        // of the layout entirely rather than leaving an empty strip behind.
        vault_->setRoot(QString());
        if (vaultSidebar_) {
            vaultSidebar_->setRoot(QString());
            vaultSidebar_->hide();
        }
        return;
    }

    if (vaults_->isEmpty()) {
        vaults_->load();
    }
    vaults_->remember(root);
    vault_->setRoot(root);
    setVaultSidebarVisible(settings_.vaultSidebarVisible);
}

void MainWindow::setVaultRoot(const QString& root) {
    const QString path = VaultRegistry::normalize(root);
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return;
    }
    settings_.vaultEnabled = true;
    settings_.vaultRoot = path;
    settings_.save();
    applyVaultSettings();
    if (vaultSidebar_ && vaultSidebar_->isVisible()) {
        vaultSidebar_->setRoot(path);
    }
    refreshChrome();
}

void MainWindow::setVaultSidebarVisible(bool visible) {
    // Zen wins: nothing but the text, whatever the vault setting says. Leaving
    // zen re-reads the setting, so the tree comes back.
    const bool wanted = visible && vault_->isOpen() && !zen_;
    if (!wanted) {
        if (vaultSidebar_) {
            vaultSidebar_->hide();
        }
        return;
    }

    if (!vaultSidebar_) {
        // First use: build it now, not at startup, so a loom without a vault
        // never pays for it.
        vaultSidebar_ = new VaultSidebar(vault_, this);
        shell_->insertWidget(0, vaultSidebar_);
        vaultSidebar_->setTheme(themes_->theme());
        vaultSidebar_->setChromeFont(Fonts::chrome(settings_, 10));
        connect(vaultSidebar_, &VaultSidebar::openRequested, this,
                [this](const QString& path) { openPathAtLine(path, 0); });
        connect(vaultSidebar_, &VaultSidebar::pathMoved, this, &MainWindow::retargetBuffers);
        connect(vaultSidebar_, &VaultSidebar::pathDeleted, this, [this](const QString&) {
            refreshChrome();
        });
        connect(vaultSidebar_, &VaultSidebar::closeRequested, this,
                [this]() { setVaultSidebarVisible(false); focusedEditor()->setFocus(); });
    }
    vaultSidebar_->setRoot(vault_->root());
    vaultSidebar_->show();
    const int width = qBound(140, settings_.vaultSidebarWidth, qMax(200, this->width() / 2));
    shell_->setSizes({width, qMax(200, this->width() - width)});
    Buffer* buffer = buffers_->current();
    vaultSidebar_->syncCurrentPath(buffer ? buffer->path() : QString());
}

void MainWindow::toggleVaultSidebar() {
    if (!vault_->isOpen() || zen_) {
        // No vault configured, or zen mode: the key does nothing at all rather
        // than advertising a feature that is off or hinting at chrome that zen
        // deliberately removed.
        return;
    }
    const bool next = !(vaultSidebar_ && vaultSidebar_->isVisible());
    if (!next && vaultSidebar_) {
        // Remember the width the user dragged to before it disappears.
        const QList<int> sizes = shell_->sizes();
        if (!sizes.isEmpty() && sizes.first() > 0) {
            settings_.vaultSidebarWidth = sizes.first();
        }
    }
    settings_.vaultSidebarVisible = next;
    settings_.save();
    setVaultSidebarVisible(next);
    if (next && vaultSidebar_) {
        vaultSidebar_->focusTree();
    } else {
        focusedEditor()->setFocus();
    }
}

// Builds the overlay on first use and returns it. Split out because three
// entry points need it, and one of them (the vault switcher) has to work while
// no vault is open.
VaultOverlay* MainWindow::ensureVaultOverlay() {
    if (!vaultOverlay_) {
        vaultOverlay_ = new VaultOverlay(centralWidget());
        vaultOverlay_->setTheme(themes_->theme());
        vaultOverlay_->setChromeFont(Fonts::chrome(settings_, 10));
        connect(vaultOverlay_, &VaultOverlay::pathChosen, this, &MainWindow::openPathAtLine);
        connect(vaultOverlay_, &VaultOverlay::vaultChosen, this, &MainWindow::setVaultRoot);
    }
    vaultOverlay_->setGeometry(centralWidget()->rect());
    return vaultOverlay_;
}

void MainWindow::openVaultNotes() {
    if (!vault_->isOpen()) {
        return;
    }
    vault_->refresh();
    ensureVaultOverlay()->openNotes(vault_->notes());
}

void MainWindow::openVaultBacklinks() {
    Buffer* buffer = buffers_->current();
    if (!vault_->isOpen() || !buffer || !vault_->contains(buffer->path())) {
        return;
    }
    // Backlinks are the only vault operation that reads file contents, so it is
    // computed here on demand rather than kept warm in the background.
    const auto links = vault_->backlinks(buffer->path());
    ensureVaultOverlay()->openBacklinks(links,
                                       QFileInfo(buffer->path()).completeBaseName());
}

void MainWindow::openVaultSwitcher() {
    if (vaults_->isEmpty()) {
        vaults_->load();
    }
    if (vaults_->isEmpty()) {
        chooseVaultFolder();
        return;
    }
    QVector<QPair<QString, QString>> rows;
    for (const VaultEntry& entry : vaults_->entries()) {
        rows.push_back({entry.name, entry.root});
    }
    // Must work with no vault open: this is how the feature gets turned back on.
    ensureVaultOverlay()->openVaults(rows, vault_->root());
}

void MainWindow::chooseVaultFolder() {
    const QString start = vault_->isOpen() ? vault_->root() : settings_.resolvedNotesDirectory();
    const QString dir =
        ThemedDialogs::getExistingDirectory(this, QStringLiteral("open vault"), start);
    if (dir.isEmpty()) {
        return;
    }
    setVaultRoot(dir);
}

void MainWindow::openPathAtLine(const QString& path, int line) {
    if (path.isEmpty()) {
        return;
    }
    // Reuse an open tab when there is one, so following the same link twice does
    // not pile up duplicates.
    int existing = -1;
    for (int i = 0; i < buffers_->count(); ++i) {
        if (Buffer* buffer = buffers_->at(i);
            buffer && !buffer->path().isEmpty()
            && QFileInfo(buffer->path()) == QFileInfo(path)) {
            existing = i;
            break;
        }
    }
    if (existing >= 0) {
        buffers_->setCurrentIndex(existing);
    } else {
        openPaths({path});
    }

    if (line > 0) {
        Editor* editor = focusedEditor();
        if (QTextDocument* doc = editor->document()) {
            const QTextBlock block = doc->findBlockByNumber(line - 1);
            if (block.isValid()) {
                QTextCursor cursor(block);
                editor->setTextCursor(cursor);
                editor->ensureCursorVisible();
            }
        }
    }
    focusedEditor()->setFocus();
    if (vaultSidebar_ && vaultSidebar_->isVisible()) {
        vaultSidebar_->syncCurrentPath(path);
    }
}

bool MainWindow::followDocumentLink(const QString& target, bool wiki) {
    Buffer* buffer = buffers_->current();
    const QString from = buffer ? buffer->path() : QString();

    QString name;
    QString anchor;
    Vault::splitAnchor(target, &name, &anchor);

    // Vault::resolve falls back to a path relative to this document whenever the
    // vault is closed or the document sits outside it, so a [[link]] written
    // inside a vault still resolves after the feature is switched off.
    QString path = vault_->resolve(target, from, wiki);

    // Only ever open something loom can edit as text. Without this, an inline
    // link to an image or any other asset would be decoded as UTF-8 into a named
    // buffer, and autosave would then write the mangled text back over the
    // original file.
    if (!path.isEmpty() && !Vault::isNote(path)) {
        return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }

    if (path.isEmpty() && wiki) {
        // An unresolved wiki target is an invitation, the way it is in Obsidian:
        // create the note and open it. Only ever inside a vault, so a plain
        // scratchpad never grows files behind the user's back.
        if (!vault_->isOpen() || !vault_->contains(from)) {
            return false;
        }
        const QString wanted = vault_->newNotePath(target, from);
        if (wanted.isEmpty()) {
            return false;
        }
        const QFileInfo info(wanted);
        QString err;
        path = vault_->createNote(info.absolutePath(), info.fileName(), &err);
        if (path.isEmpty()) {
            return false;
        }
        vault_->refresh();
    }
    if (path.isEmpty()) {
        return false;
    }

    if (buffer && !from.isEmpty() && QFileInfo(from) != QFileInfo(path)) {
        fileJumpStack_.append({from, focusedEditor()->textCursor().position()});
    }
    openPathAtLine(path, 0);

    if (!anchor.isEmpty()) {
        // "[[Note#Heading]]": land on the heading rather than the top of the file.
        Editor* editor = focusedEditor();
        if (QTextDocument* doc = editor->document()) {
            const auto entries = DocumentOutline::build(doc->toPlainText().split(QLatin1Char('\n')));
            for (const OutlineEntry& entry : entries) {
                if (entry.slug.compare(anchor.toLower(), Qt::CaseInsensitive) != 0) {
                    continue;
                }
                const QTextBlock block = doc->findBlockByNumber(entry.blockNumber);
                if (block.isValid()) {
                    QTextCursor cursor(block);
                    editor->setTextCursor(cursor);
                    editor->ensureCursorVisible();
                }
                break;
            }
        }
    }
    return true;
}

bool MainWindow::jumpBackAcrossFiles() {
    while (!fileJumpStack_.isEmpty()) {
        const auto entry = fileJumpStack_.takeLast();
        if (!QFileInfo::exists(entry.first)) {
            continue;
        }
        openPathAtLine(entry.first, 0);
        Editor* editor = focusedEditor();
        if (QTextDocument* doc = editor->document()) {
            QTextCursor cursor(doc);
            cursor.setPosition(qBound(0, entry.second, qMax(0, doc->characterCount() - 1)));
            editor->setTextCursor(cursor);
            editor->ensureCursorVisible();
        }
        return true;
    }
    return false;
}

void MainWindow::retargetBuffers(const QString& from, const QString& to) {
    const QFileInfo fromInfo(from);
    for (int i = 0; i < buffers_->count(); ++i) {
        Buffer* buffer = buffers_->at(i);
        if (!buffer || buffer->path().isEmpty()) {
            continue;
        }
        const QString path = buffer->path();
        if (QFileInfo(path) == fromInfo) {
            buffer->setPath(to);
            buffer->setTitle(QFileInfo(to).fileName());
            continue;
        }
        // A renamed folder takes every open file under it along.
        if (path.startsWith(from + QLatin1Char('/'))) {
            const QString moved = to + path.mid(from.size());
            buffer->setPath(moved);
            buffer->setTitle(QFileInfo(moved).fileName());
        }
    }
    persistSession();
    refreshChrome();
}

bool MainWindow::dispatchVaultSlash(const QString& arg) {
    const QString trimmed = arg.trimmed();
    const int space = trimmed.indexOf(QLatin1Char(' '));
    const QString verb = space > 0 ? trimmed.left(space) : trimmed;
    const QString rest = space > 0 ? trimmed.mid(space + 1).trimmed() : QString();

    if (verb.isEmpty() || verb == QLatin1String("open")) {
        if (!rest.isEmpty()) {
            setVaultRoot(rest);
            return true;
        }
        if (vault_->isOpen()) {
            openVaultNotes();
        } else {
            chooseVaultFolder();
        }
        return true;
    }
    if (verb == QLatin1String("tree") || verb == QLatin1String("sidebar")) {
        if (!vault_->isOpen()) {
            return false;
        }
        switch (parseOnOff(rest)) {
        case OnOff::Default:
            toggleVaultSidebar();
            return true;
        case OnOff::On:
            settings_.vaultSidebarVisible = true;
            settings_.save();
            setVaultSidebarVisible(true);
            return true;
        case OnOff::Off:
            settings_.vaultSidebarVisible = false;
            settings_.save();
            setVaultSidebarVisible(false);
            return true;
        case OnOff::Invalid:
            return false;
        }
    }
    if (verb == QLatin1String("off") || verb == QLatin1String("close")) {
        settings_.vaultEnabled = false;
        settings_.save();
        applyVaultSettings();
        refreshChrome();
        return true;
    }
    if (verb == QLatin1String("switch") || verb == QLatin1String("list")) {
        openVaultSwitcher();
        return true;
    }
    if (verb == QLatin1String("links") || verb == QLatin1String("backlinks")) {
        if (!vault_->isOpen()) {
            return false;
        }
        openVaultBacklinks();
        return true;
    }
    if (verb == QLatin1String("new")) {
        if (!vault_->isOpen()) {
            return false;
        }
        Buffer* buffer = buffers_->current();
        const QString from = buffer ? buffer->path() : QString();
        const QString dir = vault_->contains(from) ? QFileInfo(from).absolutePath() : vault_->root();
        if (rest.isEmpty()) {
            if (vaultSidebar_ && vaultSidebar_->isVisible()) {
                vaultSidebar_->newNoteInSelection();
                return true;
            }
            return false;
        }
        QString err;
        const QString path = vault_->createNote(dir, rest, &err);
        if (path.isEmpty()) {
            return false;
        }
        vault_->refresh();
        openPathAtLine(path, 0);
        return true;
    }
    if (verb == QLatin1String("folder")) {
        if (!vault_->isOpen() || rest.isEmpty()) {
            return false;
        }
        QString err;
        return !vault_->createFolder(vault_->root(), rest, &err).isEmpty();
    }
    if (verb == QLatin1String("reveal")) {
        if (!vault_->isOpen()) {
            return false;
        }
        Buffer* buffer = buffers_->current();
        if (!buffer || !vault_->contains(buffer->path())) {
            return false;
        }
        settings_.vaultSidebarVisible = true;
        settings_.save();
        setVaultSidebarVisible(true);
        // Zen refuses to build the tree at all, so there may be nothing to
        // reveal into. The setting is saved either way, so leaving zen shows it.
        if (!vaultSidebar_) {
            return false;
        }
        vaultSidebar_->revealPath(buffer->path());
        return true;
    }
    if (verb == QLatin1String("refresh")) {
        if (!vault_->isOpen()) {
            return false;
        }
        vault_->refresh();
        return true;
    }
    return false;
}

bool MainWindow::dispatchSlash(const QString& name, const QString& arg) {
    auto weather = [this](WeatherMode mode, const QString& value) {
        switch (parseOnOff(value)) {
        case OnOff::Default:
            editor_->setWeatherMode(mode, true);
            editorRight_->setWeatherMode(editor_->weatherMode(), false);
            syncWeatherSound();
            return true;
        case OnOff::On:
            editor_->setWeatherMode(mode, false);
            editorRight_->setWeatherMode(mode, false);
            syncWeatherSound();
            return true;
        case OnOff::Off:
            editor_->setWeatherMode(WeatherMode::Off, false);
            editorRight_->setWeatherMode(WeatherMode::Off, false);
            syncWeatherSound();
            return true;
        case OnOff::Invalid:
            return false;
        }
        return false;
    };

    if (name == QLatin1String("rain")) {
        return weather(WeatherMode::Rain, arg);
    }
    if (name == QLatin1String("storm")) {
        return weather(WeatherMode::Storm, arg);
    }
    if (name == QLatin1String("sound")) {
        switch (parseOnOff(arg)) {
        case OnOff::Default:
            weatherSound_->setWanted(!weatherSound_->wanted());
            syncWeatherSound();
            return true;
        case OnOff::On:
            weatherSound_->setWanted(true);
            syncWeatherSound();
            return true;
        case OnOff::Off:
            weatherSound_->setWanted(false);
            syncWeatherSound();
            return true;
        case OnOff::Invalid:
            return false;
        }
        return false;
    }
    if (name == QLatin1String("save")) {
        saveCurrent(false);
        return true;
    }
    if (name == QLatin1String("saveas") || name == QLatin1String("save-as")) {
        saveCurrent(true);
        return true;
    }
    if (name == QLatin1String("open")) {
        openFile();
        return true;
    }
    if (name == QLatin1String("new")) {
        newScratchTab();
        return true;
    }
    if (name == QLatin1String("close")) {
        captureViewState();
        buffers_->closeAt(buffers_->currentIndex());
        persistSession();
        return true;
    }
    if (name == QLatin1String("quit") || name == QLatin1String("exit")) {
        close();
        return true;
    }
    if (name == QLatin1String("zen")) {
        switch (parseOnOff(arg)) {
        case OnOff::Default:
            toggleZen();
            return true;
        case OnOff::On:
            if (!zen_) {
                toggleZen();
            }
            return true;
        case OnOff::Off:
            if (zen_) {
                toggleZen();
            }
            return true;
        case OnOff::Invalid:
            return false;
        }
    }
    if (name == QLatin1String("md") || name == QLatin1String("markdown")) {
        auto setMd = [this](bool on) {
            editor_->setMarkdownEnabled(on);
            editorRight_->setMarkdownEnabled(on);
        };
        switch (parseOnOff(arg)) {
        case OnOff::Default:
            setMd(!editor_->markdownEnabled());
            return true;
        case OnOff::On:
            setMd(true);
            return true;
        case OnOff::Off:
            setMd(false);
            return true;
        case OnOff::Invalid:
            return false;
        }
    }
    if (name == QLatin1String("toc")) {
        if (arg.trimmed().compare(QLatin1String("list"), Qt::CaseInsensitive) == 0) {
            openOutline();
            return true;
        }
        return insertOrRefreshToc(arg);
    }
    if (name == QLatin1String("outline")) {
        openOutline();
        return true;
    }
    if (name == QLatin1String("table")) {
        const QString trimmed = arg.trimmed();
        if (trimmed.compare(QLatin1String("align"), Qt::CaseInsensitive) == 0) {
            alignTableAtCursor();
            return true;
        }
        if (trimmed.compare(QLatin1String("row"), Qt::CaseInsensitive) == 0) {
            return focusedEditor()->insertTableRow();
        }
        if (trimmed.compare(QLatin1String("col"), Qt::CaseInsensitive) == 0
            || trimmed.compare(QLatin1String("column"), Qt::CaseInsensitive) == 0) {
            return focusedEditor()->insertTableColumn();
        }
        if (trimmed.compare(QLatin1String("delrow"), Qt::CaseInsensitive) == 0) {
            return focusedEditor()->deleteTableRow();
        }
        if (trimmed.compare(QLatin1String("delcol"), Qt::CaseInsensitive) == 0) {
            return focusedEditor()->deleteTableColumn();
        }
        return insertTableSkeleton(trimmed);
    }
    if (name == QLatin1String("pdf") || name == QLatin1String("export")) {
        const QString trimmed = arg.trimmed();
        if (trimmed.isEmpty()) {
            openPdfExport();
            return true;
        }
        for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
            if (tpl.id.compare(trimmed, Qt::CaseInsensitive) == 0) {
                exportPdf(tpl.id);
                return true;
            }
        }
        // Unknown template: reject so the editor leaves the text in place.
        return false;
    }
    if (name == QLatin1String("theme")) {
        if (arg.isEmpty()) {
            themeSwitcher_->setGeometry(centralWidget()->rect());
            themeSwitcher_->open(settings_.themeSource);
            return true;
        }
        const QString needle = arg.toLower();
        const QString slug = QString(needle).replace(QLatin1Char(' '), QLatin1Char('-'));
        for (const ThemeSpec& spec : Palettes::catalog()) {
            if (spec.id.compare(slug, Qt::CaseInsensitive) == 0 || spec.name.toLower() == needle) {
                setThemeId(spec.id);
                return true;
            }
        }
        return false;
    }
    if (name == QLatin1String("settings")) {
        openSettings();
        return true;
    }
    if (name == QLatin1String("vault")) {
        return dispatchVaultSlash(arg);
    }
    if (name == QLatin1String("help") || name == QLatin1String("keys")) {
        cheat_->setGeometry(centralWidget()->rect());
        cheat_->toggle();
        return true;
    }
    if (name == QLatin1String("find")) {
        findBar_->open(arg);
        return true;
    }
    if (name == QLatin1String("rename")) {
        renameTab();
        return true;
    }
    if (name == QLatin1String("fullscreen") || name == QLatin1String("full")) {
        switch (parseOnOff(arg)) {
        case OnOff::Default:
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
            return true;
        case OnOff::On:
            showFullScreen();
            return true;
        case OnOff::Off:
            showNormal();
            return true;
        case OnOff::Invalid:
            return false;
        }
    }
    if (name == QLatin1String("tabs")) {
        switcher_->setGeometry(centralWidget()->rect());
        switcher_->open();
        return true;
    }
    if (name == QLatin1String("zoom")) {
        if (arg.isEmpty()) {
            editor_->resetZoom();
            return true;
        }
        bool ok = false;
        const int percent = arg.toInt(&ok);
        if (!ok) {
            return false;
        }
        editor_->setZoom(percent);
        return true;
    }

    if (name == QLatin1String("radio")) {
        return dispatchRadioSlash(arg);
    }

    for (const ThemeSpec& spec : Palettes::catalog()) {
        if (spec.id == name) {
            setThemeId(spec.id);
            return true;
        }
    }
    return false;
}

bool MainWindow::dispatchRadioSlash(const QString& arg) {
    const QString trimmed = arg.trimmed();
    if (trimmed.isEmpty()) {
        openRadio();
        return true;
    }

    const int split = trimmed.indexOf(QLatin1Char(' '));
    const QString sub = (split < 0 ? trimmed : trimmed.left(split)).toLower();
    const QString rest = split < 0 ? QString() : trimmed.mid(split + 1).trimmed();

    if (sub == QLatin1String("add")) {
        radioOverlay_->setGeometry(centralWidget()->rect());
        radioOverlay_->openAdd();
        return true;
    }
    if (sub == QLatin1String("stop")) {
        radio_->stop();
        return true;
    }
    if (sub == QLatin1String("pause")) {
        radio_->pause();
        return true;
    }
    if (sub == QLatin1String("play")) {
        if (rest.isEmpty()) {
            radio_->resume();
            return true;
        }
        const RadioStation* station = stations_->findByName(rest);
        if (!station) {
            // Unknown station: reject so the editor leaves the text in place.
            return false;
        }
        radio_->play(*station);
        return true;
    }
    if (sub == QLatin1String("mini")) {
        switch (parseOnOff(rest)) {
        case OnOff::Default:
            settings_.radioMiniPlayer = !settings_.radioMiniPlayer;
            break;
        case OnOff::On:
            settings_.radioMiniPlayer = true;
            break;
        case OnOff::Off:
            settings_.radioMiniPlayer = false;
            break;
        case OnOff::Invalid:
            return false;
        }
        settings_.save();
        radioOverlay_->setMiniPlayerEnabled(settings_.radioMiniPlayer);
        syncRadioChrome();
        return true;
    }
    if (sub == QLatin1String("vol")) {
        bool ok = false;
        const int percent = rest.toInt(&ok);
        if (!ok || percent < 0 || percent > 100) {
            return false;
        }
        settings_.radioVolume = double(percent) / 100.0;
        settings_.save();
        radio_->setVolume(settings_.radioVolume);
        return true;
    }

    // Bare name: `/radio groove salad` plays it directly.
    if (const RadioStation* station = stations_->findByName(trimmed)) {
        radio_->play(*station);
        return true;
    }
    return false;
}

void MainWindow::persistSession() {
    captureViewState();
    Buffer* current = buffers_->current();
    if (current && settings_.autosaveNamedFiles && !current->path().isEmpty() && current->isDirty()) {
        AtomicFile::write(current->path(), current->text().toUtf8());
        current->markClean();
    }
    SessionStore::save(buffers_->snapshot(editor_->zoom(), saveGeometry(), zen_));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Remember the width the sidebar was dragged to, so it comes back the same.
    if (vaultSidebar_ && vaultSidebar_->isVisible()) {
        const QList<int> sizes = shell_->sizes();
        if (!sizes.isEmpty() && sizes.first() > 0
            && sizes.first() != settings_.vaultSidebarWidth) {
            settings_.vaultSidebarWidth = sizes.first();
            settings_.save();
        }
    }
    persistSession();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate) {
        persistSession();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    layoutOverlays();
}

void MainWindow::layoutOverlays() {
    if (QWidget* root = centralWidget()) {
        const QRect r = root->rect();
        cheat_->setGeometry(r);
        switcher_->setGeometry(r);
        outline_->setGeometry(r);
        themeSwitcher_->setGeometry(r);
        pdfExport_->setGeometry(r);
        radioOverlay_->setGeometry(r);
        wipe_->setGeometry(r);
        if (vaultOverlay_) {
            vaultOverlay_->setGeometry(r);
        }
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    layoutOverlays();
    if (!shown_) {
        shown_ = true;
        if (settings_.crtWipe) {
            wipe_->play();
        }
    }
}
