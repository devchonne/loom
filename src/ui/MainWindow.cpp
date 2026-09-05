#include "ui/MainWindow.h"

#include "core/AtomicFile.h"
#include "core/Buffer.h"
#include "core/BufferManager.h"
#include "core/LineDiff.h"
#include "core/Paths.h"
#include "core/PdfExport.h"
#include "core/SessionStore.h"
#include "core/SlashCommand.h"
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
#include "ui/SettingsDialog.h"
#include "ui/StatusBar.h"
#include "ui/TabStrip.h"
#include "ui/TabSwitcher.h"
#include "ui/ThemedDialogs.h"
#include "ui/ThemeSwitcher.h"
#include "ui/WeatherSound.h"

#include <QApplication>
#include <QCloseEvent>
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
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tabs_);
    layout->addWidget(splitter_, 1);
    layout->addWidget(findBar_);
    layout->addWidget(status_);
    setCentralWidget(root);

    cheat_->setParent(root);
    switcher_->setParent(root);
    outline_->setParent(root);
    themeSwitcher_->setParent(root);
    pdfExport_->setParent(root);
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

    wireShortcuts();
    applySettings(settings_);
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
    add(QKeySequence(QStringLiteral("Ctrl+K")), [this]() {
        cheat_->setGeometry(centralWidget()->rect());
        cheat_->toggle();
    });
    add(QKeySequence(QStringLiteral("Ctrl+P")), [this]() {
        switcher_->setGeometry(centralWidget()->rect());
        switcher_->open();
    });
    add(QKeySequence(QStringLiteral("Ctrl+Shift+O")), [this]() { openOutline(); });
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
    wipe_->setTheme(theme);
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
    editor_->applySettings(settings_);
    editorRight_->applySettings(settings_);
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
    persistSession();
}

void MainWindow::openSettings() {
    SettingsDialog dialog(settings_, this);
    if (dialog.exec() == QDialog::Accepted) {
        applySettings(dialog.result());
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

    for (const ThemeSpec& spec : Palettes::catalog()) {
        if (spec.id == name) {
            setThemeId(spec.id);
            return true;
        }
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
        wipe_->setGeometry(r);
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
