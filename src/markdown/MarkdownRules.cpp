#include "markdown/MarkdownRules.h"

#include <QRegularExpression>

static void addSpan(QVector<Span>& spans, int start, int length, SpanKind kind, int heading = 0) {
    if (length <= 0) {
        return;
    }
    spans.push_back(Span{start, length, kind, heading});
}

static void markerPair(QVector<Span>& spans, int open, int openLen, int close, int closeLen,
                       SpanKind innerKind, int innerStart, int innerLen, bool revealed, int heading = 0) {
    addSpan(spans, open, openLen, revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
    addSpan(spans, innerStart, innerLen, innerKind, heading);
    addSpan(spans, close, closeLen, revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
}

static void parseInline(const QString& text, int base, QVector<Span>& spans, bool revealed, int heading) {
    const int n = text.size();
    int i = 0;
    auto at = [&](int idx) -> QChar { return idx < n ? text[idx] : QChar(); };

    while (i < n) {
        if (at(i) == QLatin1Char('`')) {
            const int close = text.indexOf(QLatin1Char('`'), i + 1);
            if (close > i) {
                markerPair(spans, base + i, 1, base + close, 1, SpanKind::Code, base + i + 1,
                           close - i - 1, revealed, heading);
                i = close + 1;
                continue;
            }
        }

        if (at(i) == QLatin1Char('[')) {
            const int rb = text.indexOf(QLatin1Char(']'), i + 1);
            if (rb > i && at(rb + 1) == QLatin1Char('(')) {
                const int rp = text.indexOf(QLatin1Char(')'), rb + 2);
                if (rp > rb) {
                    addSpan(spans, base + i, 1, revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
                    addSpan(spans, base + i + 1, rb - i - 1, SpanKind::LinkText, heading);
                    addSpan(spans, base + rb, rp - rb + 1,
                            revealed ? SpanKind::LinkUrl : SpanKind::HiddenMarker);
                    i = rp + 1;
                    continue;
                }
            }
        }

        if (at(i) == QLatin1Char('~') && at(i + 1) == QLatin1Char('~')) {
            const int close = text.indexOf(QStringLiteral("~~"), i + 2);
            if (close > i) {
                markerPair(spans, base + i, 2, base + close, 2, SpanKind::Strike, base + i + 2,
                           close - i - 2, revealed, heading);
                i = close + 2;
                continue;
            }
        }

        if ((at(i) == QLatin1Char('*') && at(i + 1) == QLatin1Char('*'))
            || (at(i) == QLatin1Char('_') && at(i + 1) == QLatin1Char('_'))) {
            const QChar ch = text[i];
            const QString needle = QString(2, ch);
            const int close = text.indexOf(needle, i + 2);
            if (close > i) {
                markerPair(spans, base + i, 2, base + close, 2, SpanKind::Bold, base + i + 2,
                           close - i - 2, revealed, heading);
                i = close + 2;
                continue;
            }
        }

        if (at(i) == QLatin1Char('*') || at(i) == QLatin1Char('_')) {
            const QChar ch = text[i];
            const int close = text.indexOf(ch, i + 1);
            if (close > i && close != i + 1) {
                markerPair(spans, base + i, 1, base + close, 1, SpanKind::Italic, base + i + 1,
                           close - i - 1, revealed, heading);
                i = close + 1;
                continue;
            }
        }

        ++i;
    }
}

bool MarkdownRules::isFence(const QString& text) {
    int i = 0;
    while (i < text.size() && (text[i] == QLatin1Char(' ') || text[i] == QLatin1Char('\t'))) {
        ++i;
    }
    if (i > 3) {
        return false;
    }
    const QString rest = text.mid(i);
    return rest.startsWith(QStringLiteral("```")) || rest.startsWith(QStringLiteral("~~~"));
}

QString MarkdownRules::fenceLanguage(const QString& text) {
    int i = 0;
    while (i < text.size() && (text[i] == QLatin1Char(' ') || text[i] == QLatin1Char('\t'))) {
        ++i;
    }
    if (i > 3 || i >= text.size()) {
        return {};
    }
    const QChar tick = text[i];
    if (tick != QLatin1Char('`') && tick != QLatin1Char('~')) {
        return {};
    }
    int ticks = 0;
    while (i + ticks < text.size() && text[i + ticks] == tick) {
        ++ticks;
    }
    if (ticks < 3) {
        return {};
    }
    QString info = text.mid(i + ticks).trimmed();
    int end = 0;
    while (end < info.size()) {
        const QChar c = info[end];
        if (!(c.isLetterOrNumber() || c == QLatin1Char('+') || c == QLatin1Char('-') || c == QLatin1Char('_')
              || c == QLatin1Char('#'))) {
            break;
        }
        ++end;
    }
    return info.left(end).toLower();
}

ParseResult MarkdownRules::parseLine(const QString& text, int fenceState, bool revealed) {
    ParseResult result;
    static const QRegularExpression reSingle(
        QStringLiteral(R"(^[ \t]{0,3}(```+|~~~+)(.*)\1[ \t]*$)"));

    if (fenceState == 1) {
        if (isFence(text)) {
            result.kind = BlockKind::FenceClose;
            result.nextFenceState = 0;
            addSpan(result.spans, 0, text.size(), revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
        } else {
            result.kind = BlockKind::FenceBody;
            result.nextFenceState = 1;
            result.fenceLine = 1;
            addSpan(result.spans, 0, text.size(), SpanKind::Code);
        }
        return result;
    }

    if (const auto m = reSingle.match(text); m.hasMatch()) {
        result.kind = BlockKind::FenceSingle;
        result.nextFenceState = 0;
        result.fenceLine = 1;
        const int ticks = m.capturedLength(1);
        const int openStart = m.capturedStart(1);
        const int innerStart = m.capturedStart(2);
        const int innerLen = m.capturedLength(2);
        addSpan(result.spans, openStart, ticks, revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
        addSpan(result.spans, innerStart, innerLen, SpanKind::Code);
        addSpan(result.spans, innerStart + innerLen, ticks, revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
        return result;
    }

    if (isFence(text)) {
        result.kind = BlockKind::FenceOpen;
        result.nextFenceState = 1;
        result.fenceLang = fenceLanguage(text);
        addSpan(result.spans, 0, text.size(), revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
        return result;
    }

    static const QRegularExpression reHeading(
        QStringLiteral(R"(^( {0,3})(#{1,6})([ \t]+)(.*)$)"));
    static const QRegularExpression reRule(
        QStringLiteral(R"(^( {0,3})([-*_])\2{2,}[ \t]*$)"));
    static const QRegularExpression reQuote(
        QStringLiteral(R"(^( {0,3})(>+)([ \t]?)(.*)$)"));
    static const QRegularExpression reList(
        QStringLiteral(R"(^([ \t]*)(\*{1,6}|[+\-]|\d+[.)])([ \t]+)(?:(\[[ xX]\])([ \t]+))?(.*)$)"));

    if (const auto m = reHeading.match(text); m.hasMatch()) {
        const int hashes = m.capturedLength(2);
        result.kind = BlockKind::Heading;
        result.headingLevel = hashes;
        result.markerStart = m.capturedStart(2);
        result.markerLength = m.capturedLength(2) + m.capturedLength(3);
        addSpan(result.spans, result.markerStart, result.markerLength,
                revealed ? SpanKind::Marker : SpanKind::HiddenMarker, hashes);
        addSpan(result.spans, m.capturedStart(4), m.capturedLength(4), SpanKind::HeadingText, hashes);
        parseInline(m.captured(4), m.capturedStart(4), result.spans, revealed, hashes);
        return result;
    }

    if (reRule.match(text).hasMatch()) {
        result.kind = BlockKind::Rule;
        addSpan(result.spans, 0, text.size(),
                revealed ? SpanKind::Rule : SpanKind::HiddenMarker);
        return result;
    }

    if (const auto m = reQuote.match(text); m.hasMatch()) {
        result.kind = BlockKind::Quote;
        result.markerStart = m.capturedStart(2);
        result.markerLength = m.capturedLength(2) + m.capturedLength(3);
        addSpan(result.spans, result.markerStart, result.markerLength,
                revealed ? SpanKind::Marker : SpanKind::HiddenMarker);
        addSpan(result.spans, m.capturedStart(4), m.capturedLength(4), SpanKind::Quote);
        parseInline(m.captured(4), m.capturedStart(4), result.spans, revealed, 0);
        return result;
    }

    if (const auto m = reList.match(text); m.hasMatch()) {
        const QString marker = m.captured(2);
        const bool ordered = !marker.isEmpty() && marker[0].isDigit();
        int indentUnits = 0;
        for (const QChar c : m.captured(1)) {
            indentUnits += (c == QLatin1Char('\t')) ? 2 : 1;
        }
        int starExtra = 0;
        if (!ordered && marker.startsWith(QLatin1Char('*'))) {
            starExtra = marker.size() - 1;
        }
        result.kind = ordered ? BlockKind::OrderedList : BlockKind::List;
        result.listMarker = ordered ? QChar() : marker[0];
        result.listLevel = indentUnits / 2 + starExtra;
        result.markerStart = m.capturedStart(1);
        result.markerLength = m.capturedLength(1) + m.capturedLength(2) + m.capturedLength(3);
        const SpanKind markerKind =
            (!ordered && !revealed) ? SpanKind::HiddenMarker : SpanKind::ListMarker;
        if (!revealed) {
            addSpan(result.spans, m.capturedStart(1), m.capturedLength(1), SpanKind::HiddenMarker);
        }
        addSpan(result.spans, m.capturedStart(2),
                m.capturedLength(2) + m.capturedLength(3), markerKind);
        if (m.capturedStart(4) >= 0 && m.capturedLength(4) > 0) {
            addSpan(result.spans, m.capturedStart(4), m.capturedLength(4), SpanKind::Checkbox);
            if (!revealed && m.capturedLength(5) > 0) {
                addSpan(result.spans, m.capturedStart(5), m.capturedLength(5), SpanKind::HiddenMarker);
            }
        }
        parseInline(m.captured(6), m.capturedStart(6), result.spans, revealed, 0);
        return result;
    }

    static const QRegularExpression reImage(
        QStringLiteral(R"(^[ \t]{0,3}!\[([^\]]*)\]\(\s*<?([^)\s>]+)>?(?:\s+(?:"[^"]*"|'[^']*'))?\s*\)[ \t]*$)"));
    if (const auto m = reImage.match(text); m.hasMatch()) {
        result.kind = BlockKind::Image;
        result.imagePath = m.captured(2);
        addSpan(result.spans, 0, text.size(), SpanKind::HiddenMarker);
        return result;
    }

    parseInline(text, 0, result.spans, revealed, 0);
    return result;
}
