#include "ui/Editor.h"

#include "core/Buffer.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/SlashCommand.h"
#include "markdown/DocumentOutline.h"
#include "markdown/MarkdownHighlighter.h"
#include "markdown/RevealController.h"
#include "markdown/TableFormat.h"
#include "theme/Fonts.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHideEvent>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWheelEvent>

#include <algorithm>

namespace {

int minLinePx(const QFontMetrics& fm, double lineHeight) {
    return qMax(1, qRound(fm.height() * lineHeight));
}

bool isFenceData(const MarkdownBlockData* data) {
    if (!data) {
        return false;
    }
    return data->kind == BlockKind::FenceOpen || data->kind == BlockKind::FenceBody
        || data->kind == BlockKind::FenceClose || data->kind == BlockKind::FenceSingle;
}

// Block user data can go stale: replacing a whole table run destroys and
// recreates blocks, and the highlighter only reformats the replaced range, so a
// following paragraph may still carry a removed row's data. Table membership is
// therefore decided from the line's own syntax (the same rule MarkdownRules
// uses), with cached data consulted only to keep fenced code out.
bool isTableLine(const QTextBlock& block) {
    if (isFenceData(markdownData(block))) {
        return false;
    }
    static const QRegularExpression re(QStringLiteral(R"(^ {0,3}\|)"));
    return re.match(block.text()).hasMatch();
}

bool isTableDelimiterLine(const QString& text) {
    static const QRegularExpression re(
        QStringLiteral(R"(^ {0,3}\|(?:\s*:?-{1,}:?\s*\|)+\s*$)"));
    return re.match(text).hasMatch();
}

bool isEmptyFenceBody(const QTextBlock& block) {
    const MarkdownBlockData* data = markdownData(block);
    return data && data->kind == BlockKind::FenceBody && block.text().isEmpty();
}

QTextBlock lastPaintedFenceBlock(QTextBlock start, int caretBlock) {
    QTextBlock last = start;
    for (QTextBlock next = start.next(); next.isValid(); next = next.next()) {
        if (!isFenceData(markdownData(next))) {
            break;
        }
        last = next;
    }
    while (last.isValid() && last != start && last.blockNumber() != caretBlock && isEmptyFenceBody(last)) {
        last = last.previous();
    }
    return last;
}

} // namespace

Editor::Editor(QWidget* parent)
    : QTextEdit(parent)
    , highlighter_(new MarkdownHighlighter(this))
    , reveal_(new RevealController(this, highlighter_, this))
    , caretTimer_(new QTimer(this))
    , weatherTimer_(new QTimer(this)) {
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setUndoRedoEnabled(true);
    setCursorWidth(0);
    setMouseTracking(true);
    viewport()->setAutoFillBackground(true);
    highlighter_->setDocument(document());
    caretTimer_->setInterval(550);
    connect(caretTimer_, &QTimer::timeout, this, [this]() {
        caretVisible_ = !caretVisible_;
        viewport()->update();
    });
    caretTimer_->start();
    weatherTimer_->setInterval(33);
    connect(weatherTimer_, &QTimer::timeout, this, [this]() {
        weather_.tick();
        viewport()->update();
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
        restartCaret();
        syncListMargins();
        emit cursorInfoChanged();
    });
    connect(this, &QTextEdit::textChanged, this, [this]() {
        if (!aligningTable_) {
            realignTableAtCursor();
        }
        syncListMargins();
        if (Buffer* buffer = boundBuffer(); buffer && !buffer->isRestoring()) {
            buffer->captureHistory(textCursor().position());
        }
        emit cursorInfoChanged();
    });
}

void Editor::applySettings(const Settings& settings) {
    basePointSize_ = settings.bodyPointSize;
    lineHeight_ = settings.lineHeight;
    blockCaret_ = settings.blockCaret;
    scanlineIntensity_ = settings.scanlineIntensity;
    scanlineTile_ = {};
    const QFont font = Fonts::body(settings, basePointSize_ * zoom_ / 100.0);
    setFont(font);
    if (QTextDocument* doc = document()) {
        doc->setDefaultFont(font);
    }
    highlighter_->setBodyFamily(font.family());
    highlighter_->setBasePointSize(font.pointSizeF());
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setCursorWidth(blockCaret_ ? 0 : 1);
    applyLineHeight();
    updateZenMargins();
    viewport()->update();
}

void Editor::setTheme(const Theme& theme) {
    theme_ = theme;
    weather_.setTheme(theme);
    highlighter_->setTheme(theme);
    syncViewportFill();
    scanlineTile_ = {};
    viewport()->update();
}

void Editor::setWeatherMode(WeatherMode mode, bool toggleIfSame) {
    if (toggleIfSame && weather_.mode() == mode) {
        mode = WeatherMode::Off;
    }
    weather_.resize(viewport()->size());
    weather_.setMode(mode);
    syncViewportFill();
    syncWeatherTimer();
    viewport()->update();
}

void Editor::syncViewportFill() {
    QPalette pal = palette();
    pal.setColor(QPalette::Text, theme_.foreground);
    pal.setColor(QPalette::Highlight, theme_.selection);
    pal.setColor(QPalette::HighlightedText, theme_.brightForeground);
    pal.setColor(QPalette::Window, theme_.background);
    if (weather_.mode() != WeatherMode::Off) {
        pal.setColor(QPalette::Base, Qt::transparent);
        viewport()->setAutoFillBackground(false);
    } else {
        pal.setColor(QPalette::Base, theme_.background);
        viewport()->setAutoFillBackground(true);
    }
    setPalette(pal);
    viewport()->setPalette(pal);
}

void Editor::syncWeatherTimer() {
    if (weather_.mode() != WeatherMode::Off && isVisible()) {
        weatherTimer_->start();
    } else {
        weatherTimer_->stop();
    }
}

void Editor::bindDocument(QTextDocument* doc, bool fresh) {
    highlighter_->setDocument(nullptr);
    setDocument(doc);
    doc->setDefaultFont(font());
    highlighter_->setDocument(doc);
    highlighter_->setBodyFamily(font().family());
    highlighter_->setBasePointSize(font().pointSizeF());
    const bool modified = doc->isModified();
    applyLineHeight();
    if (fresh) {
        doc->clearUndoRedoStacks();
        doc->setModified(false);
    } else {
        doc->setModified(modified);
    }
    reveal_->reset();
    extraCarets_.clear();
    multiColumn_ = -1;
    imageCache_.clear();
    jumpStack_.clear();
    restartCaret();
}

void Editor::unbindDocument() {
    highlighter_->setDocument(nullptr);
    extraCarets_.clear();
    multiColumn_ = -1;
    imageCache_.clear();
    jumpStack_.clear();
    setExtraSelections({});
    auto* blank = new QTextDocument(this);
    setDocument(blank);
    highlighter_->setDocument(blank);
    restartCaret();
}

void Editor::setZoom(int percent) {
    percent = qBound(50, percent, 300);
    if (percent == zoom_) {
        return;
    }
    zoom_ = percent;
    QFont f = font();
    f.setPointSizeF(basePointSize_ * zoom_ / 100.0);
    setFont(f);
    if (QTextDocument* doc = document()) {
        doc->setDefaultFont(f);
    }
    highlighter_->setBasePointSize(f.pointSizeF());
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    applyLineHeight();
    updateZenMargins();
    emit zoomChanged(zoom_);
}

void Editor::zoomBy(int delta) {
    setZoom(zoom_ + delta);
}

void Editor::resetZoom() {
    setZoom(100);
}

void Editor::setZen(bool zen) {
    zen_ = zen;
    updateZenMargins();
}

void Editor::setMarkdownEnabled(bool enabled) {
    highlighter_->setEnabled(enabled);
    syncListMargins();
}

bool Editor::markdownEnabled() const {
    return highlighter_->isEnabled();
}

void Editor::applyLineHeight() {
    QTextDocument* doc = document();
    if (!doc) {
        return;
    }
    doc->setDocumentMargin(8);
    syncListMargins();
}

void Editor::syncListMargins() {
    QTextDocument* doc = document();
    if (!doc) {
        return;
    }
    const int em = qMax(6, fontMetrics().horizontalAdvance(QLatin1Char('M')));
    const int step = 2 * em;
    const bool md = highlighter_ && highlighter_->isEnabled();
    const int fenceLeft = em * 5 + 8;
    const int fenceRight = em * 8;
    const int imagePad = qRound(em * 1.6);
    const int propHeight = qRound(lineHeight_ * 100.0);
    const int minHeight = minLinePx(fontMetrics(), lineHeight_);
    const bool modified = doc->isModified();
    struct Pending {
        int position = 0;
        QTextBlockFormat fmt;
        bool resetChar = false;
    };
    QVector<Pending> pending;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        int margin = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;
        int heightValue = minHeight;
        int heightType = QTextBlockFormat::MinimumHeight;
        if (md) {
            if (const MarkdownBlockData* data = markdownData(block)) {
                if (data->kind == BlockKind::List || data->kind == BlockKind::OrderedList) {
                    margin = data->revealed ? step : step * (data->listLevel + 1);
                    if (!data->revealed && data->checkboxStart >= 0) {
                        margin += step;
                    }
                } else if (data->kind == BlockKind::Checklist) {
                    margin = data->revealed ? step : step * (data->listLevel + 1);
                } else if (data->kind == BlockKind::FenceOpen || data->kind == BlockKind::FenceBody
                           || data->kind == BlockKind::FenceClose || data->kind == BlockKind::FenceSingle) {
                    margin = fenceLeft;
                    right = fenceRight;
                    if (data->kind == BlockKind::FenceOpen || data->kind == BlockKind::FenceClose) {
                        heightType = QTextBlockFormat::ProportionalHeight;
                        heightValue = propHeight;
                    }
                } else if (data->kind == BlockKind::Image) {
                    top = imagePad;
                    bottom = imagePad;
                    heightType = QTextBlockFormat::FixedHeight;
                    heightValue = qMax(24, imageDisplaySize(data->imagePath).height());
                } else if (data->kind == BlockKind::Rule) {
                    heightType = QTextBlockFormat::ProportionalHeight;
                    heightValue = propHeight;
                } else if (data->kind == BlockKind::TableHeader || data->kind == BlockKind::TableRow
                           || data->kind == BlockKind::TableDelimiter) {
                    // Recheck the line: cached table kinds can outlive an edit,
                    // and a stale table margin on a paragraph is very visible.
                    if (isTableLine(block)) {
                        margin = em;
                        if (isTableDelimiterLine(block.text())) {
                            heightType = QTextBlockFormat::ProportionalHeight;
                            heightValue = propHeight;
                        }
                    }
                } else if (data->kind == BlockKind::TocOpen && !data->revealed) {
                    margin = step;
                    heightType = QTextBlockFormat::FixedHeight;
                    heightValue = minHeight * 2 + em;
                    top = em / 2;
                    bottom = 0;
                } else if (data->kind == BlockKind::TocClose && !data->revealed) {
                    heightType = QTextBlockFormat::ProportionalHeight;
                    heightValue = propHeight;
                }
            }
        }
        QTextBlockFormat fmt = block.blockFormat();
        const MarkdownBlockData* data = md ? markdownData(block) : nullptr;
        const bool keepHidden = data
            && (data->kind == BlockKind::Image || data->kind == BlockKind::Rule
                || data->kind == BlockKind::FenceOpen || data->kind == BlockKind::FenceClose
                || data->kind == BlockKind::FenceSingle
                || (data->kind == BlockKind::TableDelimiter && isTableDelimiterLine(block.text()))
                || data->kind == BlockKind::TocOpen || data->kind == BlockKind::TocClose);
        if (!keepHidden && block.text().isEmpty()) {
            heightType = QTextBlockFormat::FixedHeight;
            heightValue = minHeight;
        }
        const qreal typeSize = block.charFormat().fontPointSize();
        const bool tinyType = !keepHidden && typeSize > 0.0 && typeSize < 1.0;
        if (qRound(fmt.leftMargin()) == margin && qRound(fmt.rightMargin()) == right
            && qRound(fmt.topMargin()) == top && qRound(fmt.bottomMargin()) == bottom
            && qRound(fmt.lineHeight()) == heightValue && fmt.lineHeightType() == heightType
            && !tinyType) {
            continue;
        }
        fmt.setLeftMargin(margin);
        fmt.setRightMargin(right);
        fmt.setTopMargin(top);
        fmt.setBottomMargin(bottom);
        fmt.setLineHeight(heightValue, heightType);
        pending.append({block.position(), fmt, tinyType});
    }
    if (pending.isEmpty()) {
        return;
    }
    QSignalBlocker blocker(doc);
    QTextCursor cursor(doc);
    for (const Pending& change : pending) {
        cursor.setPosition(change.position);
        cursor.setBlockFormat(change.fmt);
        if (change.resetChar) {
            applyBodyCharFormat(cursor);
        }
    }
    doc->setModified(modified);
}

int Editor::wordCount() const {
    const QString text = toPlainText().trimmed();
    if (text.isEmpty()) {
        return 0;
    }
    static const QRegularExpression re(QStringLiteral("\\s+"));
    return text.split(re, Qt::SkipEmptyParts).size();
}

void Editor::updateZenMargins() {
    if (!zen_) {
        setViewportMargins(28, 20, 28, 20);
        return;
    }
    setViewportMargins(36, 36, 36, 36);
}

void Editor::restartCaret() {
    caretVisible_ = true;
    caretTimer_->start();
    viewport()->update();
}

void Editor::resizeEvent(QResizeEvent* event) {
    QTextEdit::resizeEvent(event);
    updateZenMargins();
    weather_.resize(viewport()->size());
    syncListMargins();
}

void Editor::hideEvent(QHideEvent* event) {
    QTextEdit::hideEvent(event);
    weatherTimer_->stop();
}

void Editor::showEvent(QShowEvent* event) {
    QTextEdit::showEvent(event);
    weather_.resize(viewport()->size());
    syncWeatherTimer();
}

void Editor::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        zoomBy(event->angleDelta().y() > 0 ? 10 : -10);
        event->accept();
        return;
    }
    QTextEdit::wheelEvent(event);
}

Buffer* Editor::boundBuffer() const {
    return qobject_cast<Buffer*>(document()->parent());
}

void Editor::applyHistoryCursor() {
    Buffer* buffer = boundBuffer();
    if (!buffer) {
        return;
    }
    extraCarets_.clear();
    multiColumn_ = -1;
    QTextCursor cursor(document());
    cursor.setPosition(qBound(0, buffer->cursor(), qMax(0, document()->characterCount() - 1)));
    setTextCursor(cursor);
    syncListMargins();
    restartCaret();
}

void Editor::undoEdit() {
    Buffer* buffer = boundBuffer();
    if (!buffer || !buffer->undo()) {
        return;
    }
    applyHistoryCursor();
}

void Editor::redoEdit() {
    Buffer* buffer = boundBuffer();
    if (!buffer || !buffer->redo()) {
        return;
    }
    applyHistoryCursor();
}

bool Editor::isUndoRedoKey(const QKeyEvent* event) const {
    if (!event) {
        return false;
    }
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        return true;
    }
    const Qt::KeyboardModifiers mods =
        event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_Z;
}

bool Editor::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride && isUndoRedoKey(static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    return QTextEdit::event(event);
}

void Editor::keyPressEvent(QKeyEvent* event) {
    if (isUndoRedoKey(event)) {
        if (event->matches(QKeySequence::Undo)) {
            undoEdit();
        } else {
            redoEdit();
        }
        event->accept();
        return;
    }
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool alt = event->modifiers() & Qt::AltModifier;
    if (ctrl && !alt && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (insertCellLineBreak()) {
            event->accept();
            restartCaret();
            return;
        }
        if (const auto link = linkAt(textCursor().position())) {
            followLink(*link);
        }
        event->accept();
        restartCaret();
        return;
    }
    if (alt && !ctrl && event->key() == Qt::Key_Left) {
        jumpBack();
        event->accept();
        restartCaret();
        return;
    }
    if (ctrl && (event->modifiers() & Qt::ShiftModifier) && !alt) {
        bool handled = false;
        switch (event->key()) {
        case Qt::Key_Down:
            handled = insertTableRow();
            break;
        case Qt::Key_Up:
            handled = deleteTableRow();
            break;
        case Qt::Key_Right:
            handled = insertTableColumn();
            break;
        case Qt::Key_Left:
            handled = deleteTableColumn();
            break;
        default:
            break;
        }
        if (handled) {
            event->accept();
            restartCaret();
            return;
        }
    }
    if (ctrl) {
        switch (event->key()) {
        case Qt::Key_K:
        case Qt::Key_Comma:
        case Qt::Key_P:
        case Qt::Key_T:
        case Qt::Key_M:
            event->ignore();
            return;
        case Qt::Key_W:
            if (alt) {
                event->ignore();
                return;
            }
            break;
        default:
            break;
        }
    }
    const Qt::KeyboardModifiers chord =
        event->modifiers() & (Qt::ShiftModifier | Qt::AltModifier | Qt::ControlModifier);
    if (chord == (Qt::ShiftModifier | Qt::AltModifier)
        && (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up)) {
        addCaretVertical(event->key() == Qt::Key_Down ? 1 : -1);
        event->accept();
        restartCaret();
        return;
    }
    if (extraCarets_.isEmpty() && isImageBlock(textCursor().block())) {
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            removeImageLine();
            event->accept();
            restartCaret();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QTextCursor c = textCursor();
            c.movePosition(QTextCursor::EndOfBlock);
            c.insertBlock();
            applyBodyCharFormat(c);
            setTextCursor(c);
            ensureCursorVisible();
            event->accept();
            restartCaret();
            return;
        }
        const QString typed = event->text();
        if (!ctrl && !typed.isEmpty() && typed[0].isPrint()) {
            QTextCursor c = textCursor();
            c.movePosition(QTextCursor::EndOfBlock);
            c.insertBlock();
            applyBodyCharFormat(c);
            c.insertText(typed);
            setTextCursor(c);
            event->accept();
            restartCaret();
            return;
        }
    }
    if (!extraCarets_.isEmpty()) {
        if (event->key() == Qt::Key_Escape) {
            clearMultiCarets();
            event->accept();
            restartCaret();
            return;
        }
        if (ctrl) {
            clearMultiCarets();
            QTextEdit::keyPressEvent(event);
            restartCaret();
            return;
        }
        switch (event->key()) {
        case Qt::Key_Left:
            moveCarets(QTextCursor::Left);
            break;
        case Qt::Key_Right:
            moveCarets(QTextCursor::Right);
            break;
        case Qt::Key_Up:
            moveCarets(QTextCursor::Up);
            break;
        case Qt::Key_Down:
            moveCarets(QTextCursor::Down);
            break;
        case Qt::Key_Home:
            moveCarets(QTextCursor::StartOfBlock);
            break;
        case Qt::Key_End:
            moveCarets(QTextCursor::EndOfBlock);
            break;
        case Qt::Key_Backspace:
            deleteAtCarets(false);
            break;
        case Qt::Key_Delete:
            deleteAtCarets(true);
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            insertAtCarets(QString(QLatin1Char('\n')));
            break;
        case Qt::Key_Tab:
            insertAtCarets(QString(QLatin1Char('\t')));
            break;
        default: {
            const QString typed = event->text();
            if (!typed.isEmpty() && typed[0].isPrint()) {
                insertAtCarets(typed);
                break;
            }
            QTextEdit::keyPressEvent(event);
            restartCaret();
            return;
        }
        }
        event->accept();
        restartCaret();
        return;
    }
    if (extraCarets_.isEmpty() && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)) {
        const Qt::KeyboardModifiers tabMods =
            event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (tabMods == Qt::NoModifier) {
            const bool back = event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
            if (moveToTableCell(back ? -1 : 1)) {
                event->accept();
                restartCaret();
                return;
            }
        }
    }
    if (extraCarets_.isEmpty() && (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up)) {
        const Qt::KeyboardModifiers navMods =
            event->modifiers()
            & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (navMods == Qt::NoModifier && moveToTableRow(event->key() == Qt::Key_Down ? 1 : -1)) {
            event->accept();
            restartCaret();
            return;
        }
    }
    const Qt::KeyboardModifiers chordMods =
        event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(chordMods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (chordMods == Qt::NoModifier && trySlashCommand()) {
            event->accept();
            restartCaret();
            return;
        }
        if (chordMods == Qt::NoModifier && exitTableIfOnEmptyLastRow()) {
            event->accept();
            restartCaret();
            return;
        }
        if (chordMods == Qt::NoModifier && insertTableRow()) {
            event->accept();
            restartCaret();
            return;
        }
        QTextCursor c = textCursor();
        c.beginEditBlock();
        if (c.hasSelection()) {
            c.removeSelectedText();
        }
        c.insertBlock();
        applyBodyCharFormat(c);
        c.endEditBlock();
        setTextCursor(c);
        ensureCursorVisible();
        event->accept();
        restartCaret();
        return;
    }
    const QString typed = event->text();
    if (!typed.isEmpty() && typed[0].isPrint()) {
        snapCaretIntoTableCell(CaretSnap::Typing);
    }
    QTextEdit::keyPressEvent(event);
    restartCaret();
}

void Editor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (const auto statePos = checkboxAt(event->pos())) {
            clearMultiCarets();
            toggleCheckboxAt(*statePos);
            event->accept();
            return;
        }
        const QTextCursor hit = cursorForPosition(event->pos());
        if (const auto link = linkAt(hit.position())) {
            const bool ctrl = event->modifiers() & Qt::ControlModifier;
            const bool anchor = link->target.startsWith(QLatin1Char('#'));
            const Qt::KeyboardModifiers chord =
                event->modifiers()
                & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
            if (ctrl || (anchor && chord == Qt::NoModifier)) {
                clearMultiCarets();
                if (followLink(*link)) {
                    event->accept();
                    return;
                }
            }
        }
    }
    clearMultiCarets();
    QTextEdit::mousePressEvent(event);
    snapCaretIntoTableCell(CaretSnap::Click);
}

// A double click selects the word under the pointer, and Qt treats a table's
// pipe as a word of its own. Clicking anywhere near a cell's edge therefore
// selected the invisible "|" -- which the selection highlight made visible, and
// which the next keystroke replaced, breaking the row out of the table. The
// selection is clamped to the cell's own text so it can never span or land on a
// pipe.
void Editor::mouseDoubleClickEvent(QMouseEvent* event) {
    clearMultiCarets();
    // The cell is decided from where the pointer actually is, *before* Qt's word
    // selection runs. Deriving it from the resulting selection is wrong: a click
    // near a cell's left edge selects the pipe that closes the cell *before* it,
    // so the selection start resolves to the previous cell and the caret would be
    // moved there.
    const QTextCursor hit = cursorForPosition(event->pos());
    const QTextBlock block = hit.block();
    const int probe = hit.positionInBlock();

    QTextEdit::mouseDoubleClickEvent(event);
    if (event->button() != Qt::LeftButton) {
        return;
    }
    QTextCursor c = textCursor();
    if (!isTableLine(block) || c.block() != block) {
        return;
    }
    const QString line = block.text();
    if (TableFormat::pipePositionsAt(line, probe).size() < 2) {
        return;
    }
    const int from = TableFormat::cellContentStartAt(line, probe);
    const int to = qMax(from, TableFormat::cellContentEndAt(line, probe));
    const int selStart = c.selectionStart() - block.position();
    const int selEnd = c.selectionEnd() - block.position();
    // Keep only the part of the selection that lies within this cell's text. An
    // empty cell (or a click that only caught a pipe) collapses to a caret at the
    // cell's start, so typing goes into the cell the user actually clicked.
    const int clampedStart = qBound(from, selStart, to);
    const int clampedEnd = qBound(from, selEnd, to);
    if (c.hasSelection() && clampedStart == selStart && clampedEnd == selEnd) {
        return;
    }
    c.setPosition(block.position() + clampedStart);
    if (clampedEnd > clampedStart) {
        c.setPosition(block.position() + clampedEnd, QTextCursor::KeepAnchor);
    }
    setTextCursor(c);
    restartCaret();
}

void Editor::mouseMoveEvent(QMouseEvent* event) {
    if (checkboxAt(event->pos())) {
        viewport()->setCursor(Qt::PointingHandCursor);
        QTextEdit::mouseMoveEvent(event);
        return;
    }
    const QTextCursor hit = cursorForPosition(event->pos());
    if (const auto link = linkAt(hit.position())) {
        const bool ctrl = event->modifiers() & Qt::ControlModifier;
        if (ctrl || link->target.startsWith(QLatin1Char('#'))) {
            viewport()->setCursor(Qt::PointingHandCursor);
            QTextEdit::mouseMoveEvent(event);
            return;
        }
    }
    viewport()->setCursor(Qt::IBeamCursor);
    QTextEdit::mouseMoveEvent(event);
}

void Editor::addCaretVertical(int delta) {
    QTextCursor primary = textCursor();
    if (primary.hasSelection()) {
        primary.clearSelection();
        setTextCursor(primary);
    }
    if (multiColumn_ < 0) {
        multiColumn_ = primary.positionInBlock();
    }
    QVector<int> all = extraCarets_;
    all.append(primary.position());

    int edge = all.front();
    int edgeBlock = document()->findBlock(edge).blockNumber();
    for (int p : all) {
        const int b = document()->findBlock(p).blockNumber();
        if ((delta > 0 && b > edgeBlock) || (delta < 0 && b < edgeBlock)) {
            edge = p;
            edgeBlock = b;
        }
    }

    QTextCursor c(document());
    c.setPosition(edge);
    if (!c.movePosition(delta > 0 ? QTextCursor::NextBlock : QTextCursor::PreviousBlock)) {
        return;
    }
    const int col = qMin(multiColumn_, c.block().text().size());
    c.setPosition(c.block().position() + col);
    const int np = c.position();
    const int newBlock = c.blockNumber();
    for (int p : all) {
        if (p == np || document()->findBlock(p).blockNumber() == newBlock) {
            return;
        }
    }
    extraCarets_.append(np);
    viewport()->update();
}

void Editor::clearMultiCarets() {
    extraCarets_.clear();
    multiColumn_ = -1;
}

QVector<int> Editor::allCaretPositions() const {
    QVector<int> all = extraCarets_;
    all.append(textCursor().position());
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    return all;
}

void Editor::commitCarets(QVector<int> positions, int primary) {
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    const int maxPos = qMax(0, document()->characterCount() - 1);
    extraCarets_.clear();
    for (int p : positions) {
        p = qBound(0, p, maxPos);
        if (p != primary) {
            extraCarets_.append(p);
        }
    }
    QTextCursor c(document());
    c.setPosition(qBound(0, primary, maxPos));
    setTextCursor(c);
}

void Editor::insertAtCarets(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    const QVector<int> pos = allCaretPositions();
    const int primaryOld = textCursor().position();
    int primaryNew = primaryOld;
    QVector<int> next;
    QTextCursor c(document());
    c.beginEditBlock();
    int shift = 0;
    for (int p : pos) {
        c.setPosition(p + shift);
        c.insertText(text);
        if (text.contains(QLatin1Char('\n')) && !isImageBlock(c.block())) {
            applyBodyCharFormat(c);
        }
        shift += text.size();
        next.append(c.position());
        if (p == primaryOld) {
            primaryNew = c.position();
        }
    }
    c.endEditBlock();
    commitCarets(next, primaryNew);
}

void Editor::deleteAtCarets(bool forward) {
    const QVector<int> pos = allCaretPositions();
    const int primaryOld = textCursor().position();
    int primaryNew = primaryOld;
    QVector<int> next;
    QTextCursor c(document());
    c.beginEditBlock();
    int shift = 0;
    for (int p : pos) {
        const int at = qMax(0, p + shift);
        c.setPosition(at);
        const int before = document()->characterCount();
        if (forward) {
            c.deleteChar();
        } else if (at > 0) {
            c.deletePreviousChar();
        }
        shift += document()->characterCount() - before;
        next.append(c.position());
        if (p == primaryOld) {
            primaryNew = c.position();
        }
    }
    c.endEditBlock();
    commitCarets(next, primaryNew);
}

void Editor::moveCarets(QTextCursor::MoveOperation op) {
    const int primaryOld = textCursor().position();
    int primaryNew = primaryOld;
    QVector<int> next;
    QTextCursor c(document());
    for (int p : allCaretPositions()) {
        c.setPosition(p);
        c.movePosition(op);
        next.append(c.position());
        if (p == primaryOld) {
            primaryNew = c.position();
        }
    }
    commitCarets(next, primaryNew);
}

bool Editor::trySlashCommand() {
    const auto cmd = parseSlashLine(textCursor().block().text());
    if (!cmd) {
        return false;
    }
    consumeCurrentLine();
    bool accepted = false;
    emit slashCommand(cmd->name, cmd->arg, &accepted);
    if (!accepted) {
        if (QTextDocument* doc = document()) {
            doc->undo();
        }
        return false;
    }
    return true;
}

void Editor::consumeCurrentLine() {
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.endEditBlock();
    setTextCursor(c);
}

void Editor::inputMethodEvent(QInputMethodEvent* event) {
    if (!extraCarets_.isEmpty() && !event->commitString().isEmpty()) {
        insertAtCarets(event->commitString());
        event->accept();
        restartCaret();
        return;
    }
    QTextEdit::inputMethodEvent(event);
    restartCaret();
}

void Editor::paintEvent(QPaintEvent* event) {
    if (weather_.mode() != WeatherMode::Off) {
        QPainter bg(viewport());
        weather_.paint(bg, viewport()->rect());
    }
    QTextEdit::paintEvent(event);
    QPainter painter(viewport());
    drawMarkdownChrome(painter);
    drawFenceLineNumbers(painter);
    drawScanlines(painter);
    drawVignette(painter);
    drawCaret(painter);
}

void Editor::drawMarkdownChrome(QPainter& painter) {
    if (!highlighter_ || !highlighter_->isEnabled()) {
        return;
    }
    QTextDocument* doc = document();
    if (!doc) {
        return;
    }
    QAbstractTextDocumentLayout* layout = doc->documentLayout();
    const int xOff = -horizontalScrollBar()->value();
    const int yOff = -verticalScrollBar()->value();
    const int viewH = viewport()->height();
    const int viewW = viewport()->width();
    const int charW = qMax(6, fontMetrics().horizontalAdvance(QLatin1Char('M')));
    const int caretBlock = textCursor().blockNumber();

    QColor codeBg = theme_.lighterBackground;
    codeBg.setAlpha(theme_.dark ? 110 : 90);

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QRectF br = layout->blockBoundingRect(block).translated(xOff, yOff);
        if (br.bottom() < 0) {
            continue;
        }
        if (br.top() > viewH) {
            break;
        }
        const MarkdownBlockData* data = markdownData(block);
        if (!data) {
            continue;
        }

        if (isFenceData(data)) {
            const MarkdownBlockData* prevData =
                block.previous().isValid() ? markdownData(block.previous()) : nullptr;
            if (!isFenceData(prevData)) {
                const QTextBlock paintedLast = lastPaintedFenceBlock(block, caretBlock);
                const int minH = minLinePx(fontMetrics(), lineHeight_);
                QRect run;
                bool any = false;
                for (QTextBlock b = block; b.isValid(); b = b.next()) {
                    if (b.blockNumber() > paintedLast.blockNumber()) {
                        break;
                    }
                    const MarkdownBlockData* d = markdownData(b);
                    if (d && (d->kind == BlockKind::FenceBody || d->kind == BlockKind::FenceSingle)) {
                        QTextCursor c(b);
                        c.setPosition(b.position());
                        QRect line = cursorRect(c);
                        if (line.height() < minH) {
                            line.setHeight(minH);
                        }
                        if (!b.text().isEmpty()) {
                            c.movePosition(QTextCursor::EndOfBlock);
                            line = line.united(cursorRect(c));
                        }
                        if (!any) {
                            run = line;
                            any = true;
                        } else {
                            run = run.united(line);
                        }
                    }
                    if (b == paintedLast) {
                        break;
                    }
                }
                if (any) {
                    const int insetL = charW * 2;
                    const int insetR = charW * 8;
                    const qreal pad = 4;
                    painter.fillRect(QRectF(insetL, run.top() - pad, qMax(0, viewW - insetL - insetR),
                                            run.height() + 2 * pad),
                                     codeBg);
                }
            }
        }

        if (isTableLine(block)) {
            const bool prevIsTable = block.previous().isValid() && isTableLine(block.previous());
            if (!prevIsTable) {
                QTextBlock last = block;
                for (QTextBlock next = block.next(); next.isValid(); next = next.next()) {
                    if (!isTableLine(next)) {
                        break;
                    }
                    last = next;
                }
                const QRectF lastBr = layout->blockBoundingRect(last).translated(xOff, yOff);

                QRect content;
                bool anyContent = false;
                for (QTextBlock b = block; b.isValid(); b = b.next()) {
                    if (!isTableDelimiterLine(b.text()) && !b.text().isEmpty()) {
                        QTextCursor c(b);
                        c.setPosition(b.position());
                        QRect line = cursorRect(c);
                        c.movePosition(QTextCursor::EndOfBlock);
                        line = line.united(cursorRect(c));
                        if (!anyContent) {
                            content = line;
                            anyContent = true;
                        } else {
                            content = content.united(line);
                        }
                    }
                    if (b == last) {
                        break;
                    }
                }
                if (anyContent) {
                    const int hPad = qMax(10, charW);
                    const int vPad = 5;
                    // The panel's extent comes from the caret rects of the run's
                    // first and last rows. blockBoundingRect() excludes the part
                    // of a line contributed by the line-height multiplier, so its
                    // bottom sits inside the last row's text and the border was
                    // drawn through it.
                    QTextCursor firstEdge(block);
                    firstEdge.setPosition(block.position());
                    QTextCursor lastEdge(last);
                    lastEdge.movePosition(QTextCursor::EndOfBlock);
                    const int panelTop = qMin<int>(qRound(br.top()), cursorRect(firstEdge).top());
                    const int panelBottom =
                        qMax<int>(qRound(lastBr.bottom()), cursorRect(lastEdge).bottom());
                    const QRectF panel(content.left() - hPad, panelTop - vPad,
                                       content.width() + 2 * hPad,
                                       panelBottom - panelTop + 2 * vPad);

                    painter.setRenderHint(QPainter::Antialiasing, true);
                    QPainterPath path;
                    path.addRoundedRect(panel, 5, 5);
                    painter.fillPath(path, codeBg);
                    QColor border = theme_.muted;
                    border.setAlpha(theme_.dark ? 130 : 110);
                    painter.setPen(QPen(border, 1));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(path);
                    painter.setRenderHint(QPainter::Antialiasing, false);

                    QColor grid = theme_.muted;
                    grid.setAlpha(theme_.dark ? 70 : 55);
                    QPen gridPen(grid, 1);
                    gridPen.setCosmetic(true);

                    QVector<int> colXs;
                    {
                        const QString headerLine = block.text();
                        for (int i = 0; i < headerLine.size(); ++i) {
                            if (headerLine[i] != QLatin1Char('|')) {
                                continue;
                            }
                            int backslashes = 0;
                            int j = i - 1;
                            while (j >= 0 && headerLine[j] == QLatin1Char('\\')) {
                                ++backslashes;
                                --j;
                            }
                            if (backslashes % 2 != 0) {
                                continue;
                            }
                            const int pos = qBound(block.position(), block.position() + i,
                                                   block.position() + qMax(0, block.length() - 1));
                            QTextCursor cursor(block);
                            cursor.setPosition(pos);
                            colXs.append(cursorRect(cursor).center().x());
                        }
                    }
                    const int innerL = qRound(panel.left()) + 4;
                    const int innerR = qRound(panel.right()) - 4;
                    const int gridTop = qRound(panel.top()) + 4;
                    const int gridBot = qRound(panel.bottom()) - 4;
                    painter.setPen(gridPen);
                    for (int i = 1; i + 1 < colXs.size(); ++i) {
                        const int x = colXs.at(i);
                        if (x > innerL && x < innerR) {
                            painter.drawLine(x, gridTop, x, gridBot);
                        }
                    }

                    // Horizontal rules between rows. A block's bounding rect
                    // covers *all* of a wrapped row's visual lines, so a rule
                    // can only ever land on a real row boundary -- which is the
                    // whole point: it tells a second line inside a cell apart
                    // from a second row.
                    //
                    // The y comes from the caret rects at the two rows' facing
                    // edges, not from blockBoundingRect(): with a line height
                    // multiplier applied, a block's rect bottom falls *inside*
                    // its own last text line, so a rule drawn there struck
                    // through the text.
                    for (QTextBlock b = block; b.isValid(); b = b.next()) {
                        const bool isLast = b == last;
                        if (isTableDelimiterLine(b.text())) {
                            const QRectF delimBr = layout->blockBoundingRect(b).translated(xOff, yOff);
                            const int y = qRound(delimBr.center().y());
                            painter.setPen(QPen(border, 1));
                            painter.drawLine(QPointF(panel.left() + 8, y), QPointF(panel.right() - 8, y));
                        } else if (!isLast) {
                            const QTextBlock next = b.next();
                            // The header's rule is the delimiter's own, stronger
                            // one; don't double it up.
                            const bool delimFollows = next.isValid() && isTableDelimiterLine(next.text());
                            if (next.isValid() && !delimFollows) {
                                QTextCursor above(b);
                                above.movePosition(QTextCursor::EndOfBlock);
                                QTextCursor below(next);
                                below.setPosition(next.position());
                                const int gapTop = cursorRect(above).bottom();
                                const int gapBot = cursorRect(below).top();
                                const int y = (gapTop + gapBot) / 2;
                                if (y > gridTop && y < gridBot) {
                                    painter.setPen(gridPen);
                                    painter.drawLine(QPointF(panel.left() + 8, y),
                                                     QPointF(panel.right() - 8, y));
                                }
                            }
                        }
                        if (isLast) {
                            break;
                        }
                    }
                }
            }
        }

        if (data->kind == BlockKind::TocOpen && !data->revealed) {
            QFont title = font();
            title.setPointSizeF(basePointSize_ * 1.05);
            title.setWeight(QFont::DemiBold);
            painter.setFont(title);
            painter.setPen(theme_.accent);
            const QFontMetrics fm(title);
            int x = qRound(br.left());
            for (QTextBlock n = block.next(); n.isValid(); n = n.next()) {
                const MarkdownBlockData* nd = markdownData(n);
                if (!nd || nd->kind == BlockKind::TocClose) {
                    break;
                }
                if (nd->kind == BlockKind::List || nd->kind == BlockKind::OrderedList) {
                    QTextCursor cursor(n);
                    cursor.setPosition(n.position() + qMax(0, nd->markerStart));
                    const QRect cr = cursorRect(cursor);
                    const int step = 2 * charW;
                    const int size = qMax(8, qRound(charW * (nd->listLevel == 0 ? 0.78 : 0.58)));
                    x = cr.x() - step + (step - size) / 2;
                    break;
                }
            }
            const int y = qRound(br.top()) + fm.ascent() + 2;
            painter.drawText(x, y, QStringLiteral("Table of Contents"));
            painter.setFont(font());
        }

        if (data->kind == BlockKind::List && !data->revealed) {
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + qMax(0, data->markerStart));
            const QRect cr = cursorRect(cursor);
            const int step = 2 * charW;
            // A checkbox reserves its own gutter slot right next to the text
            // (see the checkbox block below); push the bullet one slot further
            // left so the two decorations never overlap.
            const int slotBack = (data->checkboxStart >= 0) ? step * 2 : step;
            const int size = qMax(8, qRound(charW * (data->listLevel == 0 ? 0.78 : 0.58)));
            const int x = cr.x() - slotBack + (step - size) / 2;
            const int y = cr.y() + (cr.height() - size) / 2;
            const QRect dot(x, y, size, size);
            if (data->listLevel <= 0) {
                painter.fillRect(dot, theme_.accent);
            } else if (data->listLevel == 1) {
                painter.setPen(QPen(theme_.accent, qMax(2, size / 4)));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(dot.adjusted(1, 1, -1, -1));
            } else {
                painter.fillRect(dot, theme_.darkForeground);
            }
        }

        if ((data->kind == BlockKind::List || data->kind == BlockKind::OrderedList
             || data->kind == BlockKind::Checklist)
            && data->checkboxStart >= 0 && !data->revealed) {
            const QRect box = checkboxRect(block, *data, charW);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(QPen(theme_.accent, qMax(2, box.width() / 8)));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, 2, 2);
            if (data->checkboxChecked) {
                QPainterPath check;
                check.moveTo(box.left() + box.width() * 0.20, box.top() + box.height() * 0.55);
                check.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.78);
                check.lineTo(box.left() + box.width() * 0.82, box.top() + box.height() * 0.26);
                painter.strokePath(check, QPen(theme_.accent, qMax(2, box.width() / 6), Qt::SolidLine,
                                               Qt::RoundCap, Qt::RoundJoin));
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }

        if (data->kind == BlockKind::Rule && !data->revealed) {
            const int y = qRound(br.center().y());
            painter.setPen(QPen(theme_.muted, 1));
            painter.drawLine(QPointF(br.left() + 8, y), QPointF(viewW - 8, y));
        }

        if (data->kind == BlockKind::Image) {
            const int pad = qRound(charW * 1.6);
            const QSize sz = imageDisplaySize(data->imagePath);
            const QRect dest(charW * 2, qRound(br.top()) + pad, sz.width(), sz.height());
            const QPixmap pm = cachedPixmap(data->imagePath);
            if (pm.isNull()) {
                painter.setPen(theme_.muted);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(dest);
            } else {
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawPixmap(dest, pm);
            }
            if (textCursor().blockNumber() == block.blockNumber()) {
                QColor ring = theme_.accent;
                ring.setAlpha(160);
                painter.setPen(QPen(ring, 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(dest.adjusted(-2, -2, 2, 2));
            }
        }
    }
}

void Editor::drawFenceLineNumbers(QPainter& painter) {
    if (!highlighter_ || !highlighter_->isEnabled()) {
        return;
    }
    QTextDocument* doc = document();
    if (!doc) {
        return;
    }
    QAbstractTextDocumentLayout* layout = doc->documentLayout();
    const int xOff = -horizontalScrollBar()->value();
    const int yOff = -verticalScrollBar()->value();
    const int viewH = viewport()->height();
    const int em = qMax(6, fontMetrics().horizontalAdvance(QLatin1Char('M')));
    const int numW = em * 2;
    const int gap = em;

    QFont numFont = font();
    numFont.setPointSizeF(qMax(6.0, font().pointSizeF() * 0.62));
    painter.setFont(numFont);
    painter.setPen(theme_.darkForeground);
    const int caretBlock = textCursor().blockNumber();

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QRectF br = layout->blockBoundingRect(block).translated(xOff, yOff);
        if (br.bottom() < 0) {
            continue;
        }
        if (br.top() > viewH) {
            break;
        }
        const MarkdownBlockData* data = markdownData(block);
        if (!data || data->fenceLine <= 0) {
            continue;
        }
        if (data->kind != BlockKind::FenceBody && data->kind != BlockKind::FenceSingle) {
            continue;
        }
        QTextBlock runStart = block;
        while (runStart.previous().isValid() && isFenceData(markdownData(runStart.previous()))) {
            runStart = runStart.previous();
        }
        const QTextBlock paintedLast = lastPaintedFenceBlock(runStart, caretBlock);
        if (block.blockNumber() > paintedLast.blockNumber()) {
            continue;
        }
        QTextCursor cursor(block);
        cursor.setPosition(block.position());
        const QRect cr = cursorRect(cursor);
        const QRect numRect(cr.x() - gap - numW, cr.y(), numW, cr.height());
        painter.drawText(numRect, Qt::AlignVCenter | Qt::AlignRight, QString::number(data->fenceLine));
    }
}

void Editor::drawScanlines(QPainter& painter) {
    if (scanlineIntensity_ <= 0.001) {
        return;
    }
    if (scanlineTile_.isNull()) {
        scanlineTile_ = QPixmap(1, 4);
        scanlineTile_.fill(Qt::transparent);
        QPainter tile(&scanlineTile_);
        const int alpha = qBound(0, qRound(255.0 * scanlineIntensity_), 80);
        tile.fillRect(0, 3, 1, 1, QColor(0, 0, 0, alpha));
    }
    painter.drawTiledPixmap(viewport()->rect(), scanlineTile_);
}

void Editor::drawVignette(QPainter& painter) {
    if (scanlineIntensity_ <= 0.001) {
        return;
    }
    QRect r = viewport()->rect();
    QRadialGradient g(r.center(), qMax(r.width(), r.height()) * 0.72);
    g.setColorAt(0.0, Qt::transparent);
    g.setColorAt(1.0, QColor(0, 0, 0, theme_.dark ? 70 : 30));
    painter.fillRect(r, g);
}

void Editor::drawCaret(QPainter& painter) {
    if (!caretVisible_ || !hasFocus()) {
        return;
    }
    if (!isImageBlock(textCursor().block())) {
        drawOneCaret(painter, textCursor());
    }
    QTextCursor extra(document());
    for (int pos : extraCarets_) {
        extra.setPosition(qBound(0, pos, qMax(0, document()->characterCount() - 1)));
        if (!isImageBlock(extra.block())) {
            drawOneCaret(painter, extra);
        }
    }
}

void Editor::drawOneCaret(QPainter& painter, const QTextCursor& cursor) {
    QRect r = cursorRect(cursor);
    if (blockCaret_) {
        const int w = qMax(6, fontMetrics().horizontalAdvance(QLatin1Char('M')));
        r.setWidth(w);
    } else {
        r.setWidth(qMax(1, r.width()));
    }
    painter.fillRect(r, theme_.accent);
    if (!blockCaret_ || cursor.hasSelection()) {
        return;
    }
    const QString block = cursor.block().text();
    const int pos = cursor.positionInBlock();
    if (pos < block.size()) {
        painter.setPen(theme_.background);
        painter.setFont(font());
        painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, block.mid(pos, 1));
    }
}

void Editor::applyBodyCharFormat(QTextCursor& cursor) {
    QTextCharFormat fmt;
    fmt.setFont(font());
    qreal ps = font().pointSizeF();
    if (ps <= 0.0) {
        ps = basePointSize_;
    }
    fmt.setFontPointSize(ps);
    fmt.setForeground(theme_.foreground);
    fmt.setBackground(Qt::NoBrush);
    fmt.setFontLetterSpacing(100);
    fmt.setFontLetterSpacingType(QFont::PercentageSpacing);
    fmt.setFontWeight(QFont::Normal);
    fmt.setFontItalic(false);
    fmt.setFontUnderline(false);
    fmt.setFontStrikeOut(false);
    cursor.setCharFormat(fmt);
    cursor.setBlockCharFormat(fmt);
    QTextBlockFormat bf = cursor.blockFormat();
    const int px = minLinePx(fontMetrics(), lineHeight_);
    if (cursor.block().text().isEmpty()) {
        bf.setLineHeight(px, QTextBlockFormat::FixedHeight);
    } else {
        bf.setLineHeight(px, QTextBlockFormat::MinimumHeight);
    }
    bf.setTopMargin(0);
    bf.setBottomMargin(0);
    cursor.setBlockFormat(bf);
}

void Editor::setResourceDir(const QString& dir) {
    if (resourceDir_ == dir) {
        return;
    }
    resourceDir_ = dir;
    imageCache_.clear();
}

void Editor::setDiffHighlights(const QVector<QPair<int, QColor>>& tints) {
    QList<QTextEdit::ExtraSelection> sels;
    QTextDocument* doc = document();
    if (!doc) {
        setExtraSelections(sels);
        return;
    }
    for (const auto& tint : tints) {
        const QTextBlock block = doc->findBlockByNumber(tint.first);
        if (!block.isValid()) {
            continue;
        }
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(tint.second);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        sel.cursor = cursor;
        sels.append(sel);
    }
    setExtraSelections(sels);
}

bool Editor::isImageBlock(const QTextBlock& block) const {
    if (!highlighter_ || !highlighter_->isEnabled()) {
        return false;
    }
    const MarkdownBlockData* data = markdownData(block);
    return data && data->kind == BlockKind::Image;
}

void Editor::removeImageLine() {
    QTextCursor c = textCursor();
    c.beginEditBlock();
    c.movePosition(QTextCursor::StartOfBlock);
    if (c.block().next().isValid()) {
        c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    } else {
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    }
    c.removeSelectedText();
    c.endEditBlock();
    setTextCursor(c);
}

QRect Editor::checkboxRect(const QTextBlock& block, const MarkdownBlockData& data, int charW) const {
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + qMax(0, data.checkboxStart));
    const QRect cr = cursorRect(cursor);
    const int step = 2 * charW;
    const int size = qMax(10, qRound(charW * 1.1));
    // Leave a visible gap between the box's right edge and the task text
    // instead of centering it flush against the slot boundary.
    const int gap = qMax(4, qRound(charW * 0.4));
    const int x = qMax(cr.x() - step, cr.x() - gap - size);
    const int y = cr.y() + (cr.height() - size) / 2;
    return QRect(x, y, size, size);
}

std::optional<QPair<int, bool>> Editor::checkboxAt(const QPoint& pos) const {
    if (!highlighter_ || !highlighter_->isEnabled()) {
        return std::nullopt;
    }
    QTextDocument* doc = document();
    if (!doc) {
        return std::nullopt;
    }
    const QTextCursor hit = cursorForPosition(pos);
    const QTextBlock block = hit.block();
    if (!block.isValid()) {
        return std::nullopt;
    }
    const MarkdownBlockData* data = markdownData(block);
    if (!data || data->checkboxStart < 0 || data->revealed) {
        return std::nullopt;
    }
    const bool isChecklist = data->kind == BlockKind::Checklist;
    if (!isChecklist && data->kind != BlockKind::List && data->kind != BlockKind::OrderedList) {
        return std::nullopt;
    }
    const int charW = qMax(6, fontMetrics().horizontalAdvance(QLatin1Char('M')));
    const QRect box = checkboxRect(block, *data, charW);
    if (!box.contains(pos)) {
        return std::nullopt;
    }
    // Fixed-width (list-item) checkboxes always hold exactly one interior
    // character (' ' or 'x'), so the state sits right after the single '['.
    // Standalone bracket checkboxes are variable-width: `[]` has no interior
    // character at all, `[x]` has one, so the state position is right after
    // the *last* opening bracket (bracket depth == listLevel + 1).
    const int openLen = isChecklist ? data->listLevel + 1 : 1;
    return QPair<int, bool>(block.position() + data->checkboxStart + openLen, isChecklist);
}

void Editor::toggleCheckboxAt(QPair<int, bool> hit) {
    QTextDocument* doc = document();
    if (!doc) {
        return;
    }
    const int statePos = hit.first;
    const bool variableWidth = hit.second;
    if (statePos < 0 || statePos >= qMax(0, doc->characterCount() - 1)) {
        return;
    }
    QTextCursor cursor(doc);
    cursor.setPosition(statePos);
    cursor.setPosition(statePos + 1, QTextCursor::KeepAnchor);
    const QString selected = cursor.selectedText();
    const QChar current = selected.isEmpty() ? QChar() : selected.at(0);
    const bool checked = (current == QLatin1Char('x') || current == QLatin1Char('X'));
    cursor.beginEditBlock();
    if (variableWidth) {
        // `[]` <-> `[x]`: no placeholder character exists when unchecked, so
        // checking inserts one and unchecking removes it instead of replacing.
        if (checked) {
            cursor.insertText(QString());
        } else {
            cursor.setPosition(statePos);
            cursor.insertText(QStringLiteral("x"));
        }
    } else {
        cursor.insertText(checked ? QStringLiteral(" ") : QStringLiteral("x"));
    }
    cursor.endEditBlock();
}

std::optional<LinkRef> Editor::linkAt(int documentPos) const {
    QTextDocument* doc = document();
    if (!doc || !highlighter_ || !highlighter_->isEnabled()) {
        return std::nullopt;
    }
    const QTextBlock block = doc->findBlock(qBound(0, documentPos, qMax(0, doc->characterCount() - 1)));
    if (!block.isValid()) {
        return std::nullopt;
    }
    const MarkdownBlockData* data = markdownData(block);
    if (!data || data->links.isEmpty()) {
        return std::nullopt;
    }
    const int posInBlock = documentPos - block.position();
    for (const LinkRef& link : data->links) {
        if (posInBlock >= link.start && posInBlock < link.start + link.length) {
            return link;
        }
    }
    // TOC (and other list) items are almost entirely hidden-marker padding; a click on the
    // visible text can land just outside the [text](url) span. Treat a lone heading link
    // on the line as the target.
    if ((data->kind == BlockKind::List || data->kind == BlockKind::OrderedList)
        && data->links.size() == 1 && data->links.front().target.startsWith(QLatin1Char('#'))) {
        return data->links.front();
    }
    return std::nullopt;
}

bool Editor::followLink(const LinkRef& link) {
    emit linkActivated(link.target);
    if (link.target.startsWith(QLatin1Char('#'))) {
        const QString slug = link.target.mid(1).toLower();
        QTextDocument* doc = document();
        if (!doc) {
            return false;
        }
        const QStringList lines = doc->toPlainText().split(QLatin1Char('\n'));
        const auto entries = DocumentOutline::build(lines);
        for (const OutlineEntry& entry : entries) {
            if (entry.slug.compare(slug, Qt::CaseInsensitive) == 0) {
                const QTextBlock target = doc->findBlockByNumber(entry.blockNumber);
                if (!target.isValid()) {
                    return false;
                }
                jumpStack_.append(textCursor().position());
                QTextCursor c(target);
                setTextCursor(c);
                ensureCursorVisible();
                if (QAbstractTextDocumentLayout* layout = doc->documentLayout()) {
                    const QRectF br = layout->blockBoundingRect(target);
                    const int margin = viewport()->height() / 6;
                    const int wanted = qRound(br.top()) - margin;
                    verticalScrollBar()->setValue(
                        qBound(verticalScrollBar()->minimum(), wanted, verticalScrollBar()->maximum()));
                }
                restartCaret();
                return true;
            }
        }
        return false;
    }
    if (link.target.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || link.target.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        || link.target.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
        return QDesktopServices::openUrl(QUrl(link.target));
    }
    return false;
}

void Editor::jumpBack() {
    if (jumpStack_.isEmpty()) {
        return;
    }
    const int pos = jumpStack_.takeLast();
    QTextCursor c(document());
    c.setPosition(qBound(0, pos, qMax(0, document()->characterCount() - 1)));
    setTextCursor(c);
    ensureCursorVisible();
    restartCaret();
}

void Editor::snapCaretIntoTableCell(CaretSnap mode) {
    QTextCursor c = textCursor();
    if (c.hasSelection()) {
        return;
    }
    const QTextBlock block = c.block();
    // Only real table rows get snapped; prose or code that happens to contain a
    // pipe must keep normal caret behaviour.
    if (!isTableLine(block)) {
        return;
    }
    const QString line = block.text();
    const QVector<int> pipes = TableFormat::pipePositionsAt(line, c.positionInBlock());
    if (pipes.isEmpty()) {
        return;
    }
    const int pos = c.positionInBlock();
    int target = pos;
    if (pipes.size() < 2) {
        // Half-formed row: all we can do is stay behind the leading pipe, since
        // typing ahead of it would break the row out of the table.
        if (pos <= pipes.first()) {
            target = pipes.first() + 1;
            if (target < line.size() && line[target] == QLatin1Char(' ')) {
                ++target;
            }
        }
    } else {
        // Clamping happens against the cell's own visual line: a wrapped row is
        // stored as a grid, so the caret's cell is bounded by the pipes on that
        // line, not by the ones on the row's first line.
        const int col = TableFormat::cellIndex(line, pos);
        const int open = pipes[qBound(0, col, pipes.size() - 1)];
        const int close = pipes[qBound(0, col + 1, pipes.size() - 1)];
        if (mode == CaretSnap::Click) {
            // A click is a hit test, so pull it onto the cell's text: never into
            // the invisible pipes, and never adrift in the alignment padding.
            const int from = TableFormat::cellContentStartAt(line, pos);
            const int to = TableFormat::cellContentEndAt(line, pos);
            target = qBound(from, pos, qMax(from, to));
        } else {
            // While typing, snap forward off the pipe and its leading padding,
            // but never pull the caret back to the end of the text: that would
            // sit it before a space the user just typed, so the next character
            // would erase the space and run the words together.
            const int from = qBound(open + 1, TableFormat::cellContentStartAt(line, pos), close);
            target = qBound(from, pos, close);
        }
    }
    if (target != pos) {
        c.setPosition(block.position() + qMin(target, qMax(0, block.length() - 1)));
        setTextCursor(c);
    }
}

bool Editor::focusTableCell(int blockNumber, int col) {
    QTextDocument* doc = document();
    if (!doc) {
        return false;
    }
    const QTextBlock block = doc->findBlockByNumber(blockNumber);
    if (!block.isValid() || !isTableLine(block)) {
        return false;
    }
    const int target = TableFormat::positionForCellOffset(block.text(), qMax(0, col), 0);
    QTextCursor placed(block);
    placed.setPosition(block.position() + qMin(target, qMax(0, block.length() - 1)));
    setTextCursor(placed);
    ensureCursorVisible();
    restartCaret();
    return true;
}

void Editor::alignTableAtCursor() {
    realignTableAtCursor();
}

bool Editor::tableRunAtCursor(QTextBlock& start, QTextBlock& end, int& rowIndex) const {
    const QTextBlock caret = textCursor().block();
    auto partOfTable = [](const QTextBlock& b) { return isTableLine(b); };
    // Run membership follows the same syntax rule the parser uses, so the run
    // always matches the panel that gets painted. A line that merely contains a
    // pipe (prose, code) is never pulled in.
    if (!partOfTable(caret)) {
        return false;
    }
    start = caret;
    while (start.previous().isValid() && partOfTable(start.previous())) {
        start = start.previous();
    }
    end = caret;
    while (end.next().isValid() && partOfTable(end.next())) {
        end = end.next();
    }
    rowIndex = 0;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        if (b == caret) {
            break;
        }
        ++rowIndex;
        if (b == end) {
            break;
        }
    }
    return true;
}

bool Editor::moveToTableCell(int delta) {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    QVector<QTextBlock> blocks;
    QStringList rowsText;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        blocks.append(b);
        rowsText.append(b.text());
        if (b == end) {
            break;
        }
    }
    if (blocks.isEmpty()) {
        return false;
    }
    auto isDelim = [](const QTextBlock& b) { return isTableDelimiterLine(b.text()); };

    const int cols = qMax(1, TableFormat::columnCount(rowsText));
    int row = qBound(0, rowIndex, blocks.size() - 1);
    int col = TableFormat::cellIndex(blocks.at(row).text(), textCursor().positionInBlock());
    const bool forward = delta >= 0;

    if (isDelim(blocks.at(row))) {
        // The delimiter row has no editable cells; step off it entirely.
        col = forward ? cols : -1;
    } else {
        col += forward ? 1 : -1;
    }

    if (col >= cols) {
        int r = row + 1;
        while (r < blocks.size() && isDelim(blocks.at(r))) {
            ++r;
        }
        if (r >= blocks.size()) {
            // Past the final cell: grow the table, like a spreadsheet would.
            return insertTableRow();
        }
        row = r;
        col = 0;
    } else if (col < 0) {
        int r = row - 1;
        while (r >= 0 && isDelim(blocks.at(r))) {
            --r;
        }
        if (r < 0) {
            row = qBound(0, row, blocks.size() - 1);
            col = 0;
        } else {
            row = r;
            col = cols - 1;
        }
    }

    const QTextBlock dest = blocks.at(qBound(0, row, blocks.size() - 1));
    // Land on the first character of the target cell rather than selecting it,
    // so typing extends the existing text instead of replacing it. Offset 0 is
    // that cell's *own* first visual line, so tabbing out of a wrapped cell does
    // not strand the caret on the continuation line of the cell next door.
    const int from = TableFormat::positionForCellOffset(dest.text(), col, 0);
    QTextCursor placed(dest);
    placed.setPosition(dest.position() + qMin(from, qMax(0, dest.length() - 1)));
    setTextCursor(placed);
    ensureCursorVisible();
    return true;
}

// Vertical arrow movement inside a table walks whole rows and skips the
// delimiter, instead of letting Qt step onto the next visual line. Left to Qt,
// Down out of the header landed on the delimiter row -- which reveals its raw
// "| --- | --- |" markup and, because the delimiter's dashes are narrower than
// the header, put the caret past the last pipe, i.e. in a cell that does not
// exist.
bool Editor::moveToTableRow(int delta) {
    if (delta == 0) {
        return false;
    }
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    QVector<QTextBlock> blocks;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        blocks.append(b);
        if (b == end) {
            break;
        }
    }
    if (blocks.isEmpty()) {
        return false;
    }
    const QTextCursor caret = textCursor();
    if (caret.hasSelection()) {
        return false;
    }
    const int row = qBound(0, rowIndex, blocks.size() - 1);
    const QString line = blocks.at(row).text();
    const int pos = caret.positionInBlock();
    const int col = TableFormat::cellIndex(line, pos);

    // A cell with an intra-cell break has visual lines of its own; walk those
    // before leaving the row, keeping the column offset.
    const int cellLine = TableFormat::cellLineAt(line, pos);
    const int within = TableFormat::offsetInCellLineAt(line, pos);
    const int cellLineCount = TableFormat::cellLines(line, col).size();
    const int destLine = cellLine + (delta > 0 ? 1 : -1);
    if (destLine >= 0 && destLine < cellLineCount) {
        const int target = TableFormat::positionForCellLine(line, col, destLine, within);
        QTextCursor placed(blocks.at(row));
        placed.setPosition(blocks.at(row).position()
                           + qMin(target, qMax(0, blocks.at(row).length() - 1)));
        setTextCursor(placed);
        ensureCursorVisible();
        return true;
    }

    int r = row + (delta > 0 ? 1 : -1);
    while (r >= 0 && r < blocks.size() && isTableDelimiterLine(blocks.at(r).text())) {
        r += delta > 0 ? 1 : -1;
    }
    if (r < 0 || r >= blocks.size()) {
        // Off the top or bottom of the table: let the default handling carry the
        // caret out into the surrounding document.
        return false;
    }
    const QTextBlock dest = blocks.at(r);
    const int target = TableFormat::positionForCellOffset(dest.text(), col, 0);
    QTextCursor placed(dest);
    placed.setPosition(dest.position() + qMin(target, qMax(0, dest.length() - 1)));
    setTextCursor(placed);
    ensureCursorVisible();
    return true;
}

bool Editor::insertCellLineBreak() {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    const QTextBlock caretBlock = textCursor().block();
    if (isTableDelimiterLine(caretBlock.text())) {
        return false;
    }
    snapCaretIntoTableCell(CaretSnap::Typing);
    if (QTextCursor c = textCursor(); c.hasSelection()) {
        c.setPosition(c.selectionEnd());
        setTextCursor(c);
    }

    const QString line = caretBlock.text();
    const int pos = textCursor().positionInBlock();
    const int col = TableFormat::cellIndex(line, pos);
    const int cellLine = TableFormat::cellLineAt(line, pos);
    const int offset = TableFormat::offsetInCellLineAt(line, pos);

    // The break is inserted into the cell's *logical* text and the row is then
    // re-rendered, rather than pushing a bare U+2028 into the document. Typing
    // the separator straight in landed it wherever the caret happened to sit --
    // in the middle of a cell's trailing alignment padding, which is why the
    // break came out looking like a stray space and why several presses were
    // needed before the padding ran out and a line actually appeared.
    QStringList rows;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        rows.append(b.text());
        if (b == end) {
            break;
        }
    }
    const int row = qBound(0, rowIndex, rows.size() - 1);
    const int cols = qMax(1, TableFormat::columnCount(rows));
    QVector<QStringList> cellText;
    for (int c = 0; c < cols; ++c) {
        // cellLines(), not cells(): a cell whose text already ends in a break has
        // a trailing empty line that cells() drops as grid padding. Dropping it
        // here would silently swallow the previous Ctrl+Enter, which is what made
        // repeated presses look like they did nothing.
        cellText.append(TableFormat::cellLines(rows.at(row), c));
    }
    QStringList& target = cellText[qBound(0, col, cellText.size() - 1)];
    const int at = qBound(0, cellLine, target.size() - 1);
    // Split the caret's line in two at the caret. The offset is clamped to the
    // text, so a caret parked out in the trailing padding breaks after the text
    // rather than carrying a run of spaces onto the new line.
    const QString head = target.at(at).left(qMin(offset, target.at(at).size()));
    const QString tail = target.at(at).mid(qMin(offset, target.at(at).size()));
    target[at] = head;
    target.insert(at + 1, tail);

    QStringList cells;
    cells.reserve(cellText.size());
    for (const QStringList& lines : cellText) {
        cells.append(lines.join(QChar::LineSeparator));
    }
    rows[row] = TableFormat::rowFromCells(cells);

    const QStringList aligned = TableFormat::align(rows);
    // Caret goes to the start of the newly created line.
    return replaceTableRun(start, end, aligned, row, col, at + 1, 0);
}

bool Editor::exitTableIfOnEmptyLastRow() {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    const QTextBlock caretBlock = textCursor().block();
    if (caretBlock != end) {
        return false;
    }
    if (!isTableLine(caretBlock) || isTableDelimiterLine(caretBlock.text())) {
        return false;
    }
    QString stripped = caretBlock.text();
    stripped.remove(QLatin1Char('|'));
    if (!stripped.trimmed().isEmpty()) {
        return false;
    }
    // Enter on a trailing blank row leaves the table instead of adding another
    // one, so the caret is never trapped inside the run.
    QTextCursor c(caretBlock);
    c.beginEditBlock();
    c.setPosition(caretBlock.position());
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.setBlockFormat(QTextBlockFormat());
    applyBodyCharFormat(c);
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
    return true;
}

bool Editor::replaceTableRun(const QTextBlock& start, const QTextBlock& end, const QStringList& rows,
                             int caretRow, int caretCol, int caretCellLine, int offsetInCellLine) {
    QTextDocument* doc = document();
    if (!doc || rows.isEmpty()) {
        return false;
    }
    // Must be read before the edit: replacing the run destroys these blocks and
    // leaves `start` a stale handle whose blockNumber() no longer means anything.
    const int startBlockNumber = start.blockNumber();
    QTextCursor cursor(doc);
    cursor.setPosition(start.position());
    cursor.setPosition(end.position(), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.insertText(rows.join(QLatin1Char('\n')));
    cursor.endEditBlock();

    const int row = qBound(0, caretRow, rows.size() - 1);
    const QString& line = rows.at(row);
    const int target = TableFormat::positionForCellLine(line, caretCol, caretCellLine, offsetInCellLine);
    QTextBlock dest = doc->findBlockByNumber(startBlockNumber + row);
    if (dest.isValid()) {
        QTextCursor placed(dest);
        placed.setPosition(dest.position() + qMin(target, qMax(0, dest.length() - 1)));
        setTextCursor(placed);
        ensureCursorVisible();
    }
    return true;
}

void Editor::realignTableAtCursor() {
    if (!highlighter_ || !highlighter_->isEnabled()) {
        return;
    }
    if (Buffer* buffer = boundBuffer(); buffer && buffer->isRestoring()) {
        return;
    }
    // Never reflow a table the user is not actually editing. A bulk change
    // (setPlainText, file load, undo of a big edit) fires textChanged with the
    // caret still parked wherever it was, which previously let the run walk into
    // neighbouring prose and rewrite it as table rows.
    if (!isTableLine(textCursor().block())) {
        return;
    }
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return;
    }
    QStringList rows;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        rows.append(b.text());
        if (b == end) {
            break;
        }
    }
    const QString line = textCursor().block().text();
    const int col = TableFormat::cellIndex(line, textCursor().positionInBlock());
    const int cellLine = TableFormat::cellLineAt(line, textCursor().positionInBlock());
    const int offset = TableFormat::offsetInCellLineAt(line, textCursor().positionInBlock());
    const QStringList aligned = TableFormat::align(rows);
    if (aligned == rows) {
        return;
    }

    aligningTable_ = true;
    QTextDocument* doc = document();
    // Same caveat as replaceTableRun: capture the row index of the run's first
    // block while the handle is still live.
    const int startBlockNumber = start.blockNumber();
    QTextCursor cursor(doc);
    cursor.setPosition(start.position());
    cursor.setPosition(end.position(), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.joinPreviousEditBlock();
    cursor.insertText(aligned.join(QLatin1Char('\n')));
    cursor.endEditBlock();

    const int row = qBound(0, rowIndex, aligned.size() - 1);
    const int target = TableFormat::positionForCellLine(aligned.at(row), col, cellLine, offset);
    QTextBlock dest = doc->findBlockByNumber(startBlockNumber + row);
    if (dest.isValid()) {
        QTextCursor placed(dest);
        placed.setPosition(dest.position() + qMin(target, qMax(0, dest.length() - 1)));
        setTextCursor(placed);
    }
    aligningTable_ = false;
}

bool Editor::insertTableRow() {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    QStringList rows;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        rows.append(b.text());
        if (b == end) {
            break;
        }
    }
    int insertAt = rowIndex + 1;
    // Never land between the header and its delimiter row.
    if (insertAt < rows.size() && isTableDelimiterLine(rows.at(insertAt))) {
        insertAt = rowIndex + 2;
    }
    const QStringList updated = TableFormat::insertRow(rows, insertAt);
    return replaceTableRun(start, end, updated, insertAt, 0);
}

bool Editor::deleteTableRow() {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    const QTextBlock caretBlock = textCursor().block();
    if (!isTableLine(caretBlock) || isTableDelimiterLine(caretBlock.text())) {
        return false;
    }
    // Refuse to delete the header row: a table needs its header + delimiter.
    if (caretBlock == start) {
        return false;
    }
    QStringList rows;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        rows.append(b.text());
        if (b == end) {
            break;
        }
    }
    if (rows.size() <= 2) {
        return false;
    }
    const QStringList updated = TableFormat::deleteRow(rows, rowIndex);
    const int caretRow = qBound(0, rowIndex - 1, updated.size() - 1);
    return replaceTableRun(start, end, updated, caretRow, 0);
}

bool Editor::insertTableColumn() {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    QStringList rows;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        rows.append(b.text());
        if (b == end) {
            break;
        }
    }
    const int col = TableFormat::cellIndex(textCursor().block().text(), textCursor().positionInBlock());
    const QStringList updated = TableFormat::insertColumn(rows, col + 1);
    return replaceTableRun(start, end, updated, rowIndex, col + 1);
}

bool Editor::deleteTableColumn() {
    QTextBlock start;
    QTextBlock end;
    int rowIndex = 0;
    if (!tableRunAtCursor(start, end, rowIndex)) {
        return false;
    }
    QStringList rows;
    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        rows.append(b.text());
        if (b == end) {
            break;
        }
    }
    if (TableFormat::columnCount(rows) <= 1) {
        return false;
    }
    const int col = TableFormat::cellIndex(textCursor().block().text(), textCursor().positionInBlock());
    const QStringList updated = TableFormat::deleteColumn(rows, col);
    return replaceTableRun(start, end, updated, rowIndex, qMax(0, col - 1));
}

QString Editor::resolveImagePath(const QString& spec) const {
    if (spec.isEmpty()) {
        return {};
    }
    const QFileInfo given(spec);
    if (given.isAbsolute() && given.exists()) {
        return given.absoluteFilePath();
    }
    if (!resourceDir_.isEmpty()) {
        const QFileInfo rel(resourceDir_ + QLatin1Char('/') + spec);
        if (rel.exists()) {
            return rel.absoluteFilePath();
        }
    }
    const QFileInfo media(Paths::mediaDir() + QLatin1Char('/') + QFileInfo(spec).fileName());
    if (media.exists()) {
        return media.absoluteFilePath();
    }
    return {};
}

QPixmap Editor::cachedPixmap(const QString& spec) const {
    const QString path = resolveImagePath(spec);
    if (path.isEmpty()) {
        return {};
    }
    const auto it = imageCache_.constFind(path);
    if (it != imageCache_.cend()) {
        return it.value();
    }
    QPixmap pm(path);
    if (pm.isNull()) {
        return {};
    }
    imageCache_.insert(path, pm);
    return pm;
}

QSize Editor::imageDisplaySize(const QString& spec) const {
    const QPixmap pm = cachedPixmap(spec);
    const int em = qMax(6, fontMetrics().horizontalAdvance(QLatin1Char('M')));
    const int maxW = qMax(em * 10, viewport()->width() - em * 6);
    const int maxH = qBound(64, viewport()->height() * 2 / 5, 360);
    if (pm.isNull()) {
        return {qMin(180, maxW), 56};
    }
    return pm.size().scaled(maxW, maxH, Qt::KeepAspectRatio);
}

QString Editor::savePastedImage(const QImage& image) const {
    QImage img = image;
    if (img.width() > 1920 || img.height() > 1920) {
        img = img.scaled(1920, 1920, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    Paths::ensureDirectories();
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
    const QString path = Paths::mediaDir() + QLatin1Char('/') + name;
    if (!img.save(path, "PNG")) {
        return {};
    }
    return name;
}

void Editor::insertImageMarkdown(const QString& filename) {
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.positionInBlock() > 0 || !c.block().text().isEmpty()) {
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertBlock();
    }
    c.insertText(QStringLiteral("![](%1)").arg(filename));
    c.insertBlock();
    applyBodyCharFormat(c);
    c.endEditBlock();
    setTextCursor(c);
}

bool Editor::tryInsertImage(const QMimeData* source) {
    if (!source) {
        return false;
    }
    QImage img;
    if (source->hasImage()) {
        img = qvariant_cast<QImage>(source->imageData());
    }
    if (img.isNull() && source->hasUrls()) {
        for (const QUrl& url : source->urls()) {
            if (!url.isLocalFile()) {
                continue;
            }
            QImage loaded(url.toLocalFile());
            if (!loaded.isNull()) {
                img = loaded;
                break;
            }
        }
    }
    if (img.isNull()) {
        return false;
    }
    const QString name = savePastedImage(img);
    if (name.isEmpty()) {
        return false;
    }
    insertImageMarkdown(name);
    return true;
}

bool Editor::canInsertFromMimeData(const QMimeData* source) const {
    if (source && (source->hasImage() || source->hasUrls())) {
        return true;
    }
    return QTextEdit::canInsertFromMimeData(source);
}

void Editor::insertFromMimeData(const QMimeData* source) {
    if (tryInsertImage(source)) {
        return;
    }
    QTextEdit::insertFromMimeData(source);
}
