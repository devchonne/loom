#include "core/Buffer.h"

#include "markdown/TableFormat.h"

#include <QStringList>
#include <QTextBlock>

namespace {

constexpr int kMaxHistory = 2000;

// QTextDocument::toPlainText() rewrites QChar::LineSeparator (U+2028) as a real
// newline, which would turn an intra-cell line break into a separate line and
// split the table row. Walking the blocks keeps the separator intact so
// TableFormat::toMarkdown can encode it as "<br>".
QString documentPlainText(const QTextDocument* doc) {
    QStringList lines;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        lines.append(b.text());
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

Buffer::Buffer(QObject* parent)
    : Buffer(QUuid::createUuid().toString(QUuid::WithoutBraces), parent) {}

Buffer::Buffer(const QString& id, QObject* parent)
    : QObject(parent)
    , id_(id)
    , document_(new QTextDocument(this)) {
    document_->setUndoRedoEnabled(false);
    resetHistory();
    connect(document_, &QTextDocument::contentsChanged, this, [this]() {
        if (restoring_ || !document_->isModified()) {
            return;
        }
        if (!dirty_) {
            dirty_ = true;
            emit dirtyChanged();
        }
        emit contentsChanged();
    });
}

QString Buffer::text() const {
    return TableFormat::toMarkdown(documentPlainText(document_));
}

void Buffer::setTitle(const QString& title) {
    if (title_ == title) {
        return;
    }
    title_ = title;
    emit titleChanged();
}

void Buffer::setPath(const QString& path) {
    path_ = path;
}

void Buffer::setCursor(int pos) {
    cursor_ = pos;
}

void Buffer::setScroll(int value) {
    scroll_ = value;
}

void Buffer::setText(const QString& text, bool markClean) {
    restoring_ = true;
    document_->setPlainText(TableFormat::toDisplay(text));
    restoring_ = false;
    resetHistory();
    if (markClean) {
        this->markClean();
    }
}

void Buffer::markClean() {
    document_->setModified(false);
    if (dirty_) {
        dirty_ = false;
        emit dirtyChanged();
    }
}

void Buffer::markDirty() {
    document_->setModified(true);
    if (!dirty_) {
        dirty_ = true;
        emit dirtyChanged();
    }
}

void Buffer::resetHistory() {
    history_.clear();
    history_.append({text(), cursor_});
    historyIndex_ = 0;
}

void Buffer::captureHistory(int cursor) {
    if (restoring_ || history_.isEmpty()) {
        return;
    }
    const QString now = text();
    if (now == history_[historyIndex_].text) {
        history_[historyIndex_].cursor = cursor;
        return;
    }
    if (historyIndex_ < history_.size() - 1) {
        history_.resize(historyIndex_ + 1);
    }
    history_.append({now, cursor});
    historyIndex_ = history_.size() - 1;
    while (history_.size() > kMaxHistory) {
        history_.removeFirst();
        --historyIndex_;
    }
}

bool Buffer::canUndo() const {
    return historyIndex_ > 0;
}

bool Buffer::canRedo() const {
    return historyIndex_ + 1 < history_.size();
}

bool Buffer::undo() {
    if (!canUndo()) {
        return false;
    }
    --historyIndex_;
    applyHistory();
    return true;
}

bool Buffer::redo() {
    if (!canRedo()) {
        return false;
    }
    ++historyIndex_;
    applyHistory();
    return true;
}

void Buffer::applyHistory() {
    const HistEntry& entry = history_[historyIndex_];
    restoring_ = true;
    document_->setPlainText(TableFormat::toDisplay(entry.text));
    cursor_ = qBound(0, entry.cursor, qMax(0, document_->characterCount() - 1));
    restoring_ = false;
    markDirty();
    emit contentsChanged();
}
