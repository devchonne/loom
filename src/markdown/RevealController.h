#pragma once

#include <QObject>

class MarkdownHighlighter;
class QTextEdit;

class RevealController : public QObject {
    Q_OBJECT

public:
    RevealController(QTextEdit* editor, MarkdownHighlighter* highlighter, QObject* parent = nullptr);
    void reset();

private:
    void onCursorMoved();

    QTextEdit* editor_ = nullptr;
    MarkdownHighlighter* highlighter_ = nullptr;
    int previousBlock_ = -1;
};
