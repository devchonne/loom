#include "ui/Editor.h"

#include "core/Buffer.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/SlashCommand.h"
#include "markdown/MarkdownHighlighter.h"
#include "markdown/RevealController.h"
#include "theme/Fonts.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
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
    restartCaret();
}

void Editor::unbindDocument() {
    highlighter_->setDocument(nullptr);
    extraCarets_.clear();
    multiColumn_ = -1;
    imageCache_.clear();
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
                }
            }
        }
        QTextBlockFormat fmt = block.blockFormat();
        const MarkdownBlockData* data = md ? markdownData(block) : nullptr;
        const bool keepHidden = data
            && (data->kind == BlockKind::Image || data->kind == BlockKind::Rule
                || data->kind == BlockKind::FenceOpen || data->kind == BlockKind::FenceClose
                || data->kind == BlockKind::FenceSingle);
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
    const Qt::KeyboardModifiers chordMods =
        event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(chordMods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (chordMods == Qt::NoModifier && trySlashCommand()) {
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
    QTextEdit::keyPressEvent(event);
    restartCaret();
}

void Editor::mousePressEvent(QMouseEvent* event) {
    clearMultiCarets();
    QTextEdit::mousePressEvent(event);
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

        if (data->kind == BlockKind::List && !data->revealed) {
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + qMax(0, data->markerStart));
            const QRect cr = cursorRect(cursor);
            const int step = 2 * charW;
            const int size = qMax(8, qRound(charW * (data->listLevel == 0 ? 0.78 : 0.58)));
            const int x = cr.x() - step + (step - size) / 2;
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
