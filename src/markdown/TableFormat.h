#pragma once

#include <QStringList>
#include <QVector>

namespace TableFormat {

// Aligns a contiguous run of pipe-table lines (header, delimiter, body rows)
// by padding every cell to its column's display width and preserving any
// ":---", ":---:", "---:" alignment markers on the delimiter row. Rows are
// normalized to a canonical "| a | b |" leading+trailing pipe form.
QStringList align(const QStringList& rows);

// Builds a ready-to-align skeleton table with `cols` columns, a header row,
// a delimiter row, and `rows` empty body rows, joined with newlines.
QString skeleton(int cols, int rows);

int cellIndex(const QString& line, int posInLine);
// Offsets of every unescaped '|' on the line, left to right.
QVector<int> pipePositions(const QString& line);
// Same, but restricted to the visual line (U+2028-separated segment) that
// contains posInLine. A row whose cell holds an intra-cell break is rendered as
// a grid: every visual line carries the row's full set of pipes.
QVector<int> pipePositionsAt(const QString& line, int posInLine);
// Position of the start of a cell's text (offset 0 = its first character).
int positionForCellOffset(const QString& line, int col, int contentOffset);
// Caret placement inside a cell, expressed positionally: which of the cell's
// visual lines it is on, and how far into that line it sits (trailing alignment
// padding included, so a space the user just typed keeps its place). This pair
// survives a realign that changes column widths, which a single flat offset
// cannot -- padding and the following line would compete for the same numbers.
int cellLineAt(const QString& line, int posInLine);
int offsetInCellLineAt(const QString& line, int posInLine);
int positionForCellLine(const QString& line, int col, int cellLine, int offsetInLine);
int cellContentEnd(const QString& line, int col);
// First/last position of the text of the cell under posInLine, on that cell's
// own visual line.
int cellContentStartAt(const QString& line, int posInLine);
int cellContentEndAt(const QString& line, int posInLine);
// Logical cell contents of a row: intra-cell breaks come back as U+2028 inside
// the cell, whether the row is stored as a grid or as a single visual line.
QStringList cells(const QString& line);
// Canonical "| a | b |" row built from logical cell contents. A cell holding a
// U+2028 yields the multi-line grid shape.
QString rowFromCells(const QStringList& cells);
// The cell's text, one entry per visual line of the row it lives on (trailing
// blank lines included, unlike cells()).
QStringList cellLines(const QString& line, int col);
// How many visual lines the row occupies, and which one holds posInLine.
int visualLineCount(const QString& line);
int visualLineIndex(const QString& line, int posInLine);
int columnCount(const QStringList& rows);
QStringList insertRow(const QStringList& rows, int atIndex);
QStringList deleteRow(const QStringList& rows, int index);
QStringList insertColumn(const QStringList& rows, int atIndex);
QStringList deleteColumn(const QStringList& rows, int index);
QString toMarkdown(const QString& documentText);
QString toDisplay(const QString& markdown);

} // namespace TableFormat
