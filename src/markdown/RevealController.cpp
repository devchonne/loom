#include "markdown/RevealController.h"

#include "markdown/MarkdownHighlighter.h"

#include <QTextBlock>
#include <QTextEdit>

RevealController::RevealController(QTextEdit* editor, MarkdownHighlighter* highlighter, QObject* parent)
    : QObject(parent)
    , editor_(editor)
    , highlighter_(highlighter) {
    connect(editor_, &QTextEdit::cursorPositionChanged, this, &RevealController::onCursorMoved);
}

void RevealController::reset() {
    previousBlock_ = -1;
    onCursorMoved();
}

void RevealController::onCursorMoved() {
    const int block = editor_->textCursor().blockNumber();
    if (block == previousBlock_) {
        return;
    }
    const int old = previousBlock_;
    previousBlock_ = block;
    highlighter_->setRevealedBlock(block);
    if (old >= 0) {
        const QTextBlock prev = editor_->document()->findBlockByNumber(old);
        if (prev.isValid()) {
            highlighter_->rehighlightBlock(prev);
        }
    }
    const QTextBlock current = editor_->document()->findBlockByNumber(block);
    if (current.isValid()) {
        highlighter_->rehighlightBlock(current);
    }
}
