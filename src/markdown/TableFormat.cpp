#include "markdown/TableFormat.h"

#include <QRegularExpression>
#include <QVector>

namespace {

// QChar::LineSeparator (U+2028) is the in-document form of an intra-cell line
// break, and QChar::isSpace() considers it whitespace. Plain QString::trimmed()
// would therefore silently delete a user's Ctrl+Enter break during alignment, so
// cell text is trimmed with spaces/tabs only.
QString trimCell(const QString& text) {
    int start = 0;
    int end = text.size();
    auto isPad = [](QChar c) {
        return c == QLatin1Char(' ') || c == QLatin1Char('\t');
    };
    while (start < end && isPad(text[start])) {
        ++start;
    }
    while (end > start && isPad(text[end - 1])) {
        --end;
    }
    return text.mid(start, end - start);
}

// Start of a cell's text: the first non-padding character after the opening
// pipe. A cell that is entirely padding has no text, so this yields the cell's
// left edge (one past the pipe) rather than running to the closing pipe -- that
// is where the caret belongs when you click a blank cell.
int cellTextStart(const QString& line, int openPipe, int closePipe) {
    const int left = openPipe + 1;
    int i = left;
    while (i < closePipe && (line[i] == QLatin1Char(' ') || line[i] == QLatin1Char('\t'))) {
        ++i;
    }
    if (i >= closePipe) {
        // Blank cell: keep one space of breathing room after the pipe if the
        // padding provides it, otherwise sit right against it.
        return qMin(left + (closePipe > left ? 1 : 0), closePipe);
    }
    return i;
}

QVector<int> findUnescapedPipes(const QString& text) {    QVector<int> positions;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QLatin1Char('|')) {
            continue;
        }
        int backslashes = 0;
        int j = i - 1;
        while (j >= 0 && text[j] == QLatin1Char('\\')) {
            ++backslashes;
            --j;
        }
        if (backslashes % 2 == 0) {
            positions.push_back(i);
        }
    }
    return positions;
}

bool isPadChar(QChar c) {
    return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

// Alignment surrounds an intra-cell line break with padding: the line that ends
// at the break is padded out to the column width (so the closing pipe of the
// wrapped visual line stays under the one above it), and the line that follows
// is indented to the column's left edge (so the continuation renders underneath
// its own column instead of at the row's far left).
//
// Both are presentational, so they are stripped whenever a cell is parsed back
// out -- otherwise they would accumulate on every realign and inflate the
// measured column width.
QString stripBreakIndent(const QString& text) {
    if (!text.contains(QChar::LineSeparator)) {
        return text;
    }
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QChar::LineSeparator) {
            out.append(text[i]);
            continue;
        }
        while (!out.isEmpty() && isPadChar(out.back())) {
            out.chop(1);
        }
        out.append(text[i]);
        while (i + 1 < text.size() && isPadChar(text[i + 1])) {
            ++i;
        }
    }
    return out;
}

QStringList splitVisualCells(const QString& line) {
    QString body = trimCell(line);
    const QVector<int> pipes = findUnescapedPipes(body);
    QStringList cells;
    if (pipes.isEmpty()) {
        cells.append(stripBreakIndent(trimCell(body)));
        return cells;
    }
    for (int i = 0; i + 1 < pipes.size(); ++i) {
        const int start = pipes[i] + 1;
        const int length = pipes[i + 1] - start;
        cells.append(stripBreakIndent(trimCell(body.mid(start, length))));
    }
    const int lastPipe = pipes.last();
    const int tailStart = lastPipe + 1;
    if (tailStart < body.size()) {
        const QString tail = stripBreakIndent(trimCell(body.mid(tailStart)));
        if (!tail.isEmpty()) {
            cells.append(tail);
        }
    }
    if (pipes.first() > 0) {
        cells.prepend(stripBreakIndent(trimCell(body.left(pipes.first()))));
    }
    return cells;
}

struct Segment {
    int start;
    int end; // exclusive: the break that closes the visual line, or the line end
};

// The U+2028-separated visual lines of a row, in order. A row with no break is
// a single segment covering the whole line.
QVector<Segment> visualSegments(const QString& line) {
    QVector<Segment> segs;
    int start = 0;
    for (int i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == QChar::LineSeparator) {
            segs.append(Segment{start, i});
            start = i + 1;
        }
    }
    return segs;
}

// A row holding an intra-cell break is stored as a grid: one visual line per
// break, each carrying the row's *full* set of pipes, so every column keeps its
// own vertical lane and its own first line. Text loaded from disk (or written
// before the grid form existed) can still use the older shape, where the break
// sits inside one cell and the remainder of the row trails after it, so both
// shapes have to stay readable.
bool isGridRow(const QString& line) {
    const QVector<Segment> segs = visualSegments(line);
    if (segs.size() < 2) {
        return false;
    }
    for (const Segment& seg : segs) {
        int i = seg.start;
        while (i < seg.end && isPadChar(line[i])) {
            ++i;
        }
        if (i >= seg.end || line[i] != QLatin1Char('|')) {
            return false;
        }
    }
    return true;
}

QStringList splitCells(const QString& line) {
    if (!isGridRow(line)) {
        return splitVisualCells(line);
    }
    const QVector<Segment> segs = visualSegments(line);
    QVector<QStringList> perVisualLine;
    int cols = 0;
    for (const Segment& seg : segs) {
        perVisualLine.append(splitVisualCells(line.mid(seg.start, seg.end - seg.start)));
        cols = qMax(cols, perVisualLine.last().size());
    }
    auto cellAt = [&perVisualLine](int l, int c) {
        return c < perVisualLine.at(l).size() ? perVisualLine.at(l).at(c) : QString();
    };

    // A blank line inside a cell is usually grid padding, put there only because
    // a *neighbouring* cell is taller. Trailing lines where the whole row is
    // blank are different: they are breaks the user typed, and dropping them
    // would silently undo a Ctrl+Enter. Which cell they belong to is no longer
    // recoverable from the grid, so they are attributed to the last cell that
    // still has text -- the row keeps its height either way, which is all that
    // is visible; only the "<br>" placement in the saved file can differ.
    int lastContentLine = -1;
    for (int l = perVisualLine.size() - 1; l >= 0 && lastContentLine < 0; --l) {
        for (int c = 0; c < cols; ++c) {
            if (!cellAt(l, c).isEmpty()) {
                lastContentLine = l;
                break;
            }
        }
    }
    int owner = 0;
    if (lastContentLine >= 0) {
        for (int c = 0; c < cols; ++c) {
            if (!cellAt(lastContentLine, c).isEmpty()) {
                owner = c;
            }
        }
    }

    QStringList cells;
    cells.reserve(cols);
    for (int c = 0; c < cols; ++c) {
        QStringList lines;
        for (int l = 0; l < perVisualLine.size(); ++l) {
            lines.append(cellAt(l, c));
        }
        while (lines.size() > 1 && lines.last().isEmpty()) {
            lines.removeLast();
        }
        if (c == owner) {
            while (lines.size() < perVisualLine.size()) {
                lines.append(QString());
            }
        }
        cells.append(lines.join(QChar::LineSeparator));
    }
    return cells;
}

enum class Align { None, Left, Center, Right };

bool isDelimiterRow(const QStringList& cells) {
    static const QRegularExpression re(QStringLiteral(R"(^:?-+:?$)"));
    if (cells.isEmpty()) {
        return false;
    }
    for (const QString& cell : cells) {
        if (!re.match(cell).hasMatch()) {
            return false;
        }
    }
    return true;
}

Align alignOf(const QString& delimCell) {
    const bool left = delimCell.startsWith(QLatin1Char(':'));
    const bool right = delimCell.endsWith(QLatin1Char(':'));
    if (left && right) {
        return Align::Center;
    }
    if (right) {
        return Align::Right;
    }
    if (left) {
        return Align::Left;
    }
    return Align::None;
}

bool isWideChar(char16_t c) {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0x303E) || (c >= 0x3041 && c <= 0x33FF)
        || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xA000 && c <= 0xA4CF)
        || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFF60)
        || (c >= 0xFFE0 && c <= 0xFFE6);
}

int displayWidth(const QString& text) {
    QString t = text;
    static const QRegularExpression reBr(QStringLiteral(R"(<br\s*/?>)"), QRegularExpression::CaseInsensitiveOption);
    t.replace(reBr, QString(QChar::LineSeparator));
    int maxWidth = 0;
    int width = 0;
    for (const QChar& c : t) {
        if (c == QChar::LineSeparator) {
            maxWidth = qMax(maxWidth, width);
            width = 0;
            continue;
        }
        width += isWideChar(c.unicode()) ? 2 : 1;
    }
    return qMax(maxWidth, width);
}

QString padLine(const QString& text, int width, Align align) {
    const int gap = qMax(0, width - displayWidth(text));
    switch (align) {
    case Align::Right:
        return QString(gap, QLatin1Char(' ')) + text;
    case Align::Center: {
        const int leftGap = gap / 2;
        const int rightGap = gap - leftGap;
        return QString(leftGap, QLatin1Char(' ')) + text + QString(rightGap, QLatin1Char(' '));
    }
    case Align::Left:
    case Align::None:
    default:
        return text + QString(gap, QLatin1Char(' '));
    }
}

QString delimiterCell(int width, Align align) {
    const int dashCount = qMax(3, width) - (align == Align::Center ? 2 : (align == Align::None ? 0 : 1));
    const QString dashes(qMax(1, dashCount), QLatin1Char('-'));
    switch (align) {
    case Align::Left:
        return QLatin1Char(':') + dashes;
    case Align::Right:
        return dashes + QLatin1Char(':');
    case Align::Center:
        return QLatin1Char(':') + dashes + QLatin1Char(':');
    case Align::None:
    default:
        return dashes;
    }
}

} // namespace

QStringList TableFormat::align(const QStringList& rows) {
    if (rows.isEmpty()) {
        return rows;
    }

    QVector<QStringList> cellRows;
    cellRows.reserve(rows.size());
    for (const QString& row : rows) {
        cellRows.append(splitCells(row));
    }

    int delimIndex = -1;
    for (int i = 0; i < cellRows.size(); ++i) {
        if (isDelimiterRow(cellRows[i])) {
            delimIndex = i;
            break;
        }
    }

    int cols = 0;
    for (const QStringList& cells : cellRows) {
        cols = qMax(cols, cells.size());
    }
    if (cols == 0) {
        return rows;
    }

    QVector<Align> aligns(cols, Align::None);
    if (delimIndex >= 0) {
        const QStringList& delimCells = cellRows[delimIndex];
        for (int c = 0; c < delimCells.size() && c < cols; ++c) {
            aligns[c] = alignOf(delimCells[c]);
        }
    }

    QVector<int> widths(cols, 3);
    for (int i = 0; i < cellRows.size(); ++i) {
        if (i == delimIndex) {
            continue;
        }
        const QStringList& cells = cellRows[i];
        for (int c = 0; c < cells.size(); ++c) {
            widths[c] = qMax(widths[c], displayWidth(cells[c]));
        }
    }

    QStringList out;
    out.reserve(rows.size());
    for (int i = 0; i < cellRows.size(); ++i) {
        if (i == delimIndex) {
            QStringList rendered;
            for (int c = 0; c < cols; ++c) {
                rendered.append(delimiterCell(widths[c], aligns[c]));
            }
            out.append(QLatin1String("| ") + rendered.join(QLatin1String(" | ")) + QLatin1String(" |"));
            continue;
        }
        // A row whose cell carries an intra-cell break is laid out as a grid:
        // one complete, fully piped visual line per break. Every column then
        // keeps its own lane (the painted borders stay straight) and, just as
        // importantly, every column still starts on the row's *first* line, so
        // Tab into a neighbouring cell does not drop the caret onto the
        // continuation of the cell next door.
        const QStringList& cells = cellRows[i];
        QVector<QStringList> cellLines;
        cellLines.reserve(cols);
        int visualLines = 1;
        for (int c = 0; c < cols; ++c) {
            const QString cell = c < cells.size() ? cells[c] : QString();
            cellLines.append(cell.split(QChar::LineSeparator));
            visualLines = qMax(visualLines, cellLines.last().size());
        }
        // A trailing blank line is not attributed to any cell (it would show up
        // as a stray "<br>" on save), but it is still a line the user made with
        // Ctrl+Enter and the caret may be sitting on it, so the row keeps the
        // height it already had.
        if (isGridRow(rows.at(i))) {
            visualLines = qMax(visualLines, visualSegments(rows.at(i)).size());
        }
        QStringList visual;
        visual.reserve(visualLines);
        for (int l = 0; l < visualLines; ++l) {
            QStringList rendered;
            rendered.reserve(cols);
            for (int c = 0; c < cols; ++c) {
                const QString text = l < cellLines[c].size() ? cellLines[c].at(l) : QString();
                rendered.append(padLine(text, widths[c], aligns[c]));
            }
            visual.append(QLatin1String("| ") + rendered.join(QLatin1String(" | ")) + QLatin1String(" |"));
        }
        out.append(visual.join(QChar::LineSeparator));
    }
    return out;
}

QString TableFormat::skeleton(int cols, int rows) {
    cols = qMax(1, cols);
    rows = qMax(0, rows);

    QStringList lines;
    QStringList header;
    QStringList delim;
    for (int c = 0; c < cols; ++c) {
        // Single word on purpose: a double-click selects the whole placeholder,
        // so it can be replaced by typing. "Header 1" would need a manual
        // selection just to overwrite it.
        header.append(QStringLiteral("Col%1").arg(c + 1));
        delim.append(QStringLiteral("---"));
    }
    lines.append(QLatin1String("| ") + header.join(QLatin1String(" | ")) + QLatin1String(" |"));
    lines.append(QLatin1String("| ") + delim.join(QLatin1String(" | ")) + QLatin1String(" |"));
    for (int r = 0; r < rows; ++r) {
        QStringList body;
        for (int c = 0; c < cols; ++c) {
            body.append(QString());
        }
        lines.append(QLatin1String("| ") + body.join(QLatin1String(" | ")) + QLatin1String(" |"));
    }

    return TableFormat::align(lines).join(QLatin1Char('\n'));
}

namespace {

// The pipes of the visual line that contains `pos`, as absolute offsets.
QVector<int> pipesOnSegment(const QString& line, int pos) {
    const QVector<Segment> segs = visualSegments(line);
    pos = qBound(0, pos, line.size());
    Segment seg = segs.isEmpty() ? Segment{0, int(line.size())} : segs.first();
    for (const Segment& s : segs) {
        if (pos <= s.end) {
            seg = s;
            break;
        }
        seg = s;
    }
    QVector<int> pipes;
    for (int p : findUnescapedPipes(line.mid(seg.start, seg.end - seg.start))) {
        pipes.append(seg.start + p);
    }
    return pipes;
}

// One entry per visual line the cell spans. `start` is the cell's text start,
// `contentEnd` the end of its text on that line, `limit` its closing pipe (so
// trailing alignment padding stays addressable -- the caret has to be able to
// sit in it, otherwise a space the user just typed is overwritten by the next
// character).
struct CellSpan {
    int start;
    int contentEnd;
    int limit;
};

int trimmedEnd(const QString& line, int start, int limit) {
    int end = limit;
    while (end > start && isPadChar(line[end - 1])) {
        --end;
    }
    return qMax(start, end);
}

QVector<CellSpan> cellSpans(const QString& line, int col) {
    QVector<CellSpan> spans;
    if (col < 0) {
        return spans;
    }
    const bool grid = isGridRow(line);
    const QVector<Segment> segs =
        grid ? visualSegments(line) : QVector<Segment>{Segment{0, int(line.size())}};
    for (const Segment& seg : segs) {
        QVector<int> pipes;
        for (int p : findUnescapedPipes(line.mid(seg.start, seg.end - seg.start))) {
            pipes.append(seg.start + p);
        }
        if (col + 1 >= pipes.size()) {
            continue;
        }
        const int limit = pipes[col + 1];
        const int start = cellTextStart(line, pipes[col], limit);
        spans.append(CellSpan{start, trimmedEnd(line, start, limit), limit});
    }
    return spans;
}

} // namespace

int TableFormat::cellIndex(const QString& line, int posInLine) {
    const QVector<int> pipes = pipesOnSegment(line, posInLine);
    if (pipes.size() < 2) {
        return 0;
    }
    posInLine = qBound(0, posInLine, line.size());
    for (int i = 0; i + 1 < pipes.size(); ++i) {
        if (posInLine <= pipes[i + 1]) {
            return i;
        }
    }
    return qMax(0, pipes.size() - 2);
}

int TableFormat::positionForCellOffset(const QString& line, int col, int contentOffset) {
    const QVector<CellSpan> spans = cellSpans(line, col);
    if (spans.isEmpty()) {
        return 0;
    }
    int remaining = qMax(0, contentOffset);
    for (int i = 0; i < spans.size(); ++i) {
        const CellSpan& span = spans[i];
        // A visual line owns one offset per addressable position (its text plus
        // the trailing alignment padding the caret may sit in) and one more for
        // the break that ends it.
        const int width = span.limit - span.start;
        if (i + 1 == spans.size() || remaining <= width) {
            return qMin(span.limit, span.start + remaining);
        }
        remaining -= width + 1;
    }
    return spans.last().contentEnd;
}

QVector<int> TableFormat::pipePositions(const QString& line) {
    return findUnescapedPipes(line);
}

int TableFormat::cellLineAt(const QString& line, int posInLine) {
    const QVector<CellSpan> spans = cellSpans(line, cellIndex(line, posInLine));
    posInLine = qBound(0, posInLine, line.size());
    for (int i = 0; i < spans.size(); ++i) {
        if (posInLine <= spans[i].limit) {
            return i;
        }
    }
    return qMax(0, spans.size() - 1);
}

int TableFormat::offsetInCellLineAt(const QString& line, int posInLine) {
    const int col = cellIndex(line, posInLine);
    const QVector<CellSpan> spans = cellSpans(line, col);
    const int idx = cellLineAt(line, posInLine);
    if (idx < 0 || idx >= spans.size()) {
        return 0;
    }
    const CellSpan& span = spans[idx];
    return qMax(0, qBound(span.start, qBound(0, posInLine, line.size()), span.limit) - span.start);
}

int TableFormat::positionForCellLine(const QString& line, int col, int cellLine, int offsetInLine) {
    const QVector<CellSpan> spans = cellSpans(line, col);
    if (spans.isEmpty()) {
        return 0;
    }
    const CellSpan& span = spans[qBound(0, cellLine, spans.size() - 1)];
    return qMin(span.limit, span.start + qMax(0, offsetInLine));
}
QVector<int> TableFormat::pipePositionsAt(const QString& line, int posInLine) {
    return pipesOnSegment(line, posInLine);
}

int TableFormat::cellContentEnd(const QString& line, int col) {
    const QVector<CellSpan> spans = cellSpans(line, col);
    if (spans.isEmpty()) {
        return 0;
    }
    return spans.first().contentEnd;
}

int TableFormat::cellContentStartAt(const QString& line, int posInLine) {
    const QVector<int> pipes = pipesOnSegment(line, posInLine);
    const int col = cellIndex(line, posInLine);
    if (col + 1 >= pipes.size()) {
        return qBound(0, posInLine, line.size());
    }
    return cellTextStart(line, pipes[col], pipes[col + 1]);
}

int TableFormat::cellContentEndAt(const QString& line, int posInLine) {
    const QVector<int> pipes = pipesOnSegment(line, posInLine);
    const int col = cellIndex(line, posInLine);
    if (col + 1 >= pipes.size()) {
        return qBound(0, posInLine, line.size());
    }
    const int limit = pipes[col + 1];
    return trimmedEnd(line, cellTextStart(line, pipes[col], limit), limit);
}

QStringList TableFormat::cells(const QString& line) {
    return splitCells(line);
}

QString TableFormat::rowFromCells(const QStringList& cells) {
    int height = 1;
    for (const QString& cell : cells) {
        height = qMax(height, cell.count(QChar::LineSeparator) + 1);
    }
    if (height == 1) {
        return QLatin1String("| ") + cells.join(QLatin1String(" | ")) + QLatin1String(" |");
    }
    // Emit the grid shape straight away. A cell whose text merely *ends* with a
    // break would otherwise produce a row that reads back as "| a | b | ccc" plus
    // a stray "|" line, and the break would be parsed away again.
    QVector<QStringList> lines;
    lines.reserve(cells.size());
    for (const QString& cell : cells) {
        lines.append(cell.split(QChar::LineSeparator));
    }
    QStringList visual;
    visual.reserve(height);
    for (int l = 0; l < height; ++l) {
        QStringList rendered;
        rendered.reserve(lines.size());
        for (const QStringList& cellLines : lines) {
            rendered.append(l < cellLines.size() ? cellLines.at(l) : QString());
        }
        visual.append(QLatin1String("| ") + rendered.join(QLatin1String(" | ")) + QLatin1String(" |"));
    }
    return visual.join(QChar::LineSeparator);
}

QStringList TableFormat::cellLines(const QString& line, int col) {
    if (!isGridRow(line)) {
        return splitCells(line).value(qMax(0, col)).split(QChar::LineSeparator);
    }
    QStringList out;
    for (const CellSpan& span : cellSpans(line, col)) {
        out.append(line.mid(span.start, span.contentEnd - span.start));
    }
    return out.isEmpty() ? QStringList{QString()} : out;
}

int TableFormat::visualLineCount(const QString& line) {
    return visualSegments(line).size();
}

int TableFormat::visualLineIndex(const QString& line, int posInLine) {
    const QVector<Segment> segs = visualSegments(line);
    posInLine = qBound(0, posInLine, line.size());
    for (int i = 0; i < segs.size(); ++i) {
        if (posInLine <= segs[i].end) {
            return i;
        }
    }
    return qMax(0, segs.size() - 1);
}

int TableFormat::columnCount(const QStringList& rows) {    int cols = 0;
    for (const QString& row : rows) {
        cols = qMax(cols, splitCells(row).size());
    }
    return cols;
}

QStringList TableFormat::insertRow(const QStringList& rows, int atIndex) {
    const int cols = qMax(1, columnCount(rows));
    QStringList empty;
    for (int c = 0; c < cols; ++c) {
        empty.append(QString());
    }
    const QString line = QLatin1String("| ") + empty.join(QLatin1String(" | ")) + QLatin1String(" |");
    QStringList out = rows;
    const int idx = qBound(0, atIndex, out.size());
    out.insert(idx, line);
    return align(out);
}

QStringList TableFormat::deleteRow(const QStringList& rows, int index) {
    if (index < 0 || index >= rows.size()) {
        return rows;
    }
    QStringList out = rows;
    out.removeAt(index);
    return out.isEmpty() ? out : align(out);
}

QStringList TableFormat::insertColumn(const QStringList& rows, int atIndex) {
    if (rows.isEmpty()) {
        return rows;
    }
    QVector<QStringList> cellRows;
    int cols = 0;
    for (const QString& row : rows) {
        cellRows.append(splitCells(row));
        cols = qMax(cols, cellRows.last().size());
    }
    const int idx = qBound(0, atIndex, cols);
    for (int i = 0; i < cellRows.size(); ++i) {
        while (cellRows[i].size() < cols) {
            cellRows[i].append(QString());
        }
        const QString filler = isDelimiterRow(cellRows[i]) ? QStringLiteral("---") : QString();
        cellRows[i].insert(idx, filler);
    }
    QStringList rebuilt;
    for (const QStringList& cells : cellRows) {
        // rowFromCells, not a plain join: a cell may carry an intra-cell break,
        // which has to come back out as the grid shape rather than a row with a
        // stray separator in the middle of it.
        rebuilt.append(rowFromCells(cells));
    }
    return align(rebuilt);
}

QStringList TableFormat::deleteColumn(const QStringList& rows, int index) {
    if (rows.isEmpty()) {
        return rows;
    }
    QVector<QStringList> cellRows;
    int cols = 0;
    for (const QString& row : rows) {
        cellRows.append(splitCells(row));
        cols = qMax(cols, cellRows.last().size());
    }
    if (cols <= 1 || index < 0 || index >= cols) {
        return rows;
    }
    for (int i = 0; i < cellRows.size(); ++i) {
        if (index < cellRows[i].size()) {
            cellRows[i].removeAt(index);
        }
    }
    QStringList rebuilt;
    for (const QStringList& cells : cellRows) {
        rebuilt.append(rowFromCells(cells));
    }
    return align(rebuilt);
}

QString TableFormat::toMarkdown(const QString& documentText) {
    QStringList lines = documentText.split(QLatin1Char('\n'));
    auto isRow = [](const QString& line) { return line.trimmed().startsWith(QLatin1Char('|')); };

    for (int i = 0; i < lines.size();) {
        if (!isRow(lines.at(i))) {
            ++i;
            continue;
        }
        int end = i;
        bool hasBreak = false;
        while (end < lines.size() && isRow(lines.at(end))) {
            hasBreak = hasBreak || lines.at(end).contains(QChar::LineSeparator);
            ++end;
        }
        if (!hasBreak) {
            // Nothing to collapse: leave the author's own spacing alone.
            i = end;
            continue;
        }
        // A wrapped row occupies several fully piped visual lines on screen; on
        // disk it is one row whose cell carries "<br>". Rebuilding from the
        // logical cells is the only way to fold the grid back up, and a realign
        // afterwards keeps the saved file tidy.
        QStringList rebuilt;
        for (int r = i; r < end; ++r) {
            QStringList cells = splitCells(lines.at(r));
            for (QString& cell : cells) {
                cell.replace(QChar::LineSeparator, QStringLiteral("<br>"));
            }
            rebuilt.append(QLatin1String("| ") + cells.join(QLatin1String(" | "))
                           + QLatin1String(" |"));
        }
        const QStringList aligned = align(rebuilt);
        for (int r = i; r < end; ++r) {
            lines[r] = aligned.value(r - i, rebuilt.at(r - i));
        }
        i = end;
    }
    return lines.join(QLatin1Char('\n'));
}

QString TableFormat::toDisplay(const QString& markdown) {
    static const QRegularExpression reBr(QStringLiteral(R"(<br\s*/?>)"), QRegularExpression::CaseInsensitiveOption);
    QStringList lines = markdown.split(QLatin1Char('\n'));
    auto isRow = [](const QString& line) { return line.trimmed().startsWith(QLatin1Char('|')); };

    for (int i = 0; i < lines.size();) {
        if (!isRow(lines.at(i))) {
            ++i;
            continue;
        }
        int end = i;
        bool hasBreak = false;
        while (end < lines.size() && isRow(lines.at(end))) {
            hasBreak = hasBreak || lines.at(end).contains(reBr);
            ++end;
        }
        if (hasBreak) {
            // Expand "<br>" into the on-screen grid straight away, so a freshly
            // loaded table navigates exactly like one that has been edited.
            QStringList rebuilt;
            for (int r = i; r < end; ++r) {
                QString line = lines.at(r);
                line.replace(reBr, QString(QChar::LineSeparator));
                rebuilt.append(line);
            }
            const QStringList aligned = align(rebuilt);
            for (int r = i; r < end; ++r) {
                lines[r] = aligned.value(r - i, rebuilt.at(r - i));
            }
        }
        i = end;
    }
    return lines.join(QLatin1Char('\n'));
}
