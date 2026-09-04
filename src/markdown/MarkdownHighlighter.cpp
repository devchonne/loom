#include "markdown/MarkdownHighlighter.h"

#include "markdown/CodeSyntax.h"
#include "markdown/MarkdownRules.h"

#include <QFont>
#include <QTextBlock>

MarkdownHighlighter::MarkdownHighlighter(QObject* parent)
    : QSyntaxHighlighter(parent) {}

void MarkdownHighlighter::setTheme(const Theme& theme) {
    theme_ = theme;
    rehighlight();
}

void MarkdownHighlighter::setBasePointSize(qreal size) {
    if (qFuzzyCompare(basePointSize_, size)) {
        return;
    }
    basePointSize_ = size;
    rehighlight();
}

void MarkdownHighlighter::setBodyFamily(const QString& family) {
    if (bodyFamily_ == family) {
        return;
    }
    bodyFamily_ = family;
    rehighlight();
}

void MarkdownHighlighter::setEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    rehighlight();
}

void MarkdownHighlighter::setRevealedBlock(int blockNumber) {
    revealedBlock_ = blockNumber;
}

qreal MarkdownHighlighter::headingSize(int level) const {
    static const qreal multipliers[] = {1.9, 1.6, 1.35, 1.2, 1.1, 1.0};
    const int idx = qBound(1, level, 6) - 1;
    return basePointSize_ * multipliers[idx];
}

QTextCharFormat MarkdownHighlighter::formatFor(SpanKind kind, int headingLevel) const {
    QTextCharFormat fmt;
    fmt.setFontFamilies({bodyFamily_});
    fmt.setFontPointSize(headingLevel > 0 ? headingSize(headingLevel) : basePointSize_);
    fmt.setForeground(theme_.foreground);

    switch (kind) {
    case SpanKind::HiddenMarker:
        fmt.setForeground(QColor(0, 0, 0, 0));
        fmt.setFontLetterSpacingType(QFont::PercentageSpacing);
        fmt.setFontLetterSpacing(1.0);
        fmt.setFontPointSize(0.01);
        break;
    case SpanKind::Marker:
    case SpanKind::ListMarker:
        fmt.setForeground(theme_.darkForeground);
        fmt.setFontPointSize(basePointSize_ * 0.9);
        break;
    case SpanKind::HeadingText:
        fmt.setFontWeight(QFont::DemiBold);
        fmt.setForeground(theme_.brightForeground);
        if (headingLevel <= 2) {
            fmt.setForeground(theme_.accent);
        }
        break;
    case SpanKind::Bold:
        fmt.setFontWeight(QFont::Black);
        fmt.setForeground(theme_.accent);
        if (headingLevel <= 0) {
            fmt.setFontPointSize(basePointSize_ * 1.12);
        }
        break;
    case SpanKind::Italic:
        fmt.setFontItalic(true);
        break;
    case SpanKind::Strike:
        fmt.setFontStrikeOut(true);
        fmt.setForeground(theme_.darkForeground);
        break;
    case SpanKind::Code:
        fmt.setForeground(theme_.brightForeground);
        fmt.setBackground(theme_.lighterBackground);
        fmt.setFontPointSize(basePointSize_ * 0.92);
        break;
    case SpanKind::LinkText:
        fmt.setForeground(theme_.blue);
        fmt.setFontUnderline(true);
        break;
    case SpanKind::AnchorLinkText:
        fmt.setForeground(theme_.magenta);
        fmt.setFontUnderline(true);
        break;
    case SpanKind::LinkUrl:
        fmt.setForeground(theme_.cyan);
        fmt.setFontPointSize(basePointSize_ * 0.85);
        break;
    case SpanKind::Quote:
        fmt.setForeground(theme_.darkForeground);
        fmt.setFontItalic(true);
        break;
    case SpanKind::Checkbox:
        fmt.setForeground(theme_.yellow);
        break;
    case SpanKind::Rule:
        fmt.setForeground(theme_.muted);
        break;
    case SpanKind::TablePipe:
        fmt.setForeground(QColor(0, 0, 0, 0));
        fmt.setFontPointSize(basePointSize_);
        break;
    case SpanKind::TableHeaderText:
        fmt.setFontWeight(QFont::DemiBold);
        fmt.setForeground(theme_.brightForeground);
        break;
    case SpanKind::TableCellText:
        fmt.setForeground(theme_.foreground);
        break;
    }
    return fmt;
}

void MarkdownHighlighter::highlightBlock(const QString& text) {
    auto* data = new MarkdownBlockData();
    if (!enabled_) {
        setCurrentBlockState(0);
        currentBlock().setUserData(data);
        return;
    }

    const int prev = previousBlockState() >= 0 ? previousBlockState() : StateNone;
    const int blockNo = currentBlock().blockNumber();
    const bool revealed = (blockNo == revealedBlock_);
    const ParseResult parsed = MarkdownRules::parseLine(text, prev, revealed);
    setCurrentBlockState(parsed.nextFenceState);

    data->kind = parsed.kind;
    data->headingLevel = parsed.headingLevel;
    data->markerStart = parsed.markerStart;
    data->listMarker = parsed.listMarker;
    data->listLevel = parsed.listLevel;
    data->checkboxStart = parsed.checkboxStart;
    data->checkboxChecked = parsed.checkboxChecked;
    data->revealed = revealed;
    data->fenceLine = parsed.fenceLine;
    data->fenceLang = parsed.fenceLang;
    data->imagePath = parsed.imagePath;
    data->links = parsed.links;
    data->tableCells = parsed.tableCells;
    data->tablePipes = parsed.tablePipes;

    if (parsed.kind == BlockKind::FenceBody) {
        const QTextBlock prevBlock = currentBlock().previous();
        if (const MarkdownBlockData* prevData = markdownData(prevBlock)) {
            data->fenceLang = prevData->fenceLang;
            if (prevData->kind == BlockKind::FenceOpen) {
                data->fenceLine = 1;
            } else if (prevData->kind == BlockKind::FenceBody) {
                data->fenceLine = prevData->fenceLine + 1;
            } else {
                data->fenceLine = 1;
            }
        } else {
            data->fenceLine = 1;
        }
    }

    currentBlock().setUserData(data);

    const bool fenceCode = parsed.kind == BlockKind::FenceBody || parsed.kind == BlockKind::FenceSingle;
    for (const Span& span : parsed.spans) {
        QTextCharFormat fmt = formatFor(span.kind, span.headingLevel);
        if (fenceCode && span.kind == SpanKind::Code) {
            fmt.setBackground(Qt::NoBrush);
        }
        setFormat(span.start, span.length, fmt);
    }

    if (fenceCode && !data->fenceLang.isEmpty()) {
        for (const CodeToken& tok : CodeSyntax::tokenize(text, data->fenceLang)) {
            QTextCharFormat fmt = formatFor(SpanKind::Code, 0);
            fmt.setBackground(Qt::NoBrush);
            switch (tok.kind) {
            case CodeTokenKind::Keyword:
                fmt.setForeground(theme_.accent);
                break;
            case CodeTokenKind::Builtin:
                fmt.setForeground(theme_.cyan);
                break;
            case CodeTokenKind::String:
                fmt.setForeground(theme_.green);
                break;
            case CodeTokenKind::Comment:
                fmt.setForeground(theme_.darkForeground);
                fmt.setFontItalic(true);
                break;
            case CodeTokenKind::Number:
                fmt.setForeground(theme_.yellow);
                break;
            }
            setFormat(tok.start, tok.length, fmt);
        }
    }
}
