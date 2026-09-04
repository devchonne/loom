#include "markdown/TableFormat.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>

#include <gtest/gtest.h>

TEST(TableFormat, AlignBasicIdempotent) {
    const QStringList rows = {
        QStringLiteral("| a | bb |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| 1 | 22 |"),
    };
    const QStringList aligned = TableFormat::align(rows);
    ASSERT_EQ(aligned.size(), 3);
    // Columns pad up to a minimum width of 3 so the delimiter row reads as "---".
    EXPECT_EQ(aligned[0], QStringLiteral("| a   | bb  |"));
    EXPECT_EQ(aligned[1], QStringLiteral("| --- | --- |"));
    EXPECT_EQ(aligned[2], QStringLiteral("| 1   | 22  |"));

    // Idempotent: re-aligning already-aligned rows produces the same result.
    const QStringList realigned = TableFormat::align(aligned);
    EXPECT_EQ(realigned, aligned);
}

TEST(TableFormat, AlignRaggedRowsPadMissingCells) {
    const QStringList rows = {
        QStringLiteral("| Name | Age |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| Alice |"),
        QStringLiteral("| Bob | 42 | extra |"),
    };
    const QStringList aligned = TableFormat::align(rows);
    ASSERT_EQ(aligned.size(), 4);
    // All rows should have the same cell-count / width now (3 columns due to "extra").
    for (const QString& row : aligned) {
        EXPECT_TRUE(row.startsWith(QLatin1Char('|')));
        EXPECT_TRUE(row.endsWith(QLatin1Char('|')));
    }
    EXPECT_TRUE(aligned[2].contains(QStringLiteral("Alice")));
}

TEST(TableFormat, AlignPreservesColonAlignment) {
    const QStringList rows = {
        QStringLiteral("| Left | Center | Right |"),
        QStringLiteral("|:---|:---:|---:|"),
        QStringLiteral("| a | b | c |"),
    };
    const QStringList aligned = TableFormat::align(rows);
    ASSERT_EQ(aligned.size(), 3);
    const QStringList delimCells = aligned[1].split(QLatin1Char('|'), Qt::SkipEmptyParts);
    ASSERT_EQ(delimCells.size(), 3);
    EXPECT_TRUE(delimCells[0].trimmed().startsWith(QLatin1Char(':')));
    EXPECT_FALSE(delimCells[0].trimmed().endsWith(QLatin1Char(':')));
    EXPECT_TRUE(delimCells[1].trimmed().startsWith(QLatin1Char(':')));
    EXPECT_TRUE(delimCells[1].trimmed().endsWith(QLatin1Char(':')));
    EXPECT_FALSE(delimCells[2].trimmed().startsWith(QLatin1Char(':')));
    EXPECT_TRUE(delimCells[2].trimmed().endsWith(QLatin1Char(':')));
}

TEST(TableFormat, AlignHandlesWideCjkCells) {
    const QStringList rows = {
        QStringLiteral("| 名前 | Age |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| a | 1 |"),
    };
    const QStringList aligned = TableFormat::align(rows);
    ASSERT_EQ(aligned.size(), 3);
    // Column 1 header is 2 wide-chars (width 4); body cell "a" (width 1) should be padded
    // so the row still lines up (checked indirectly via consistent delimiter width >= 4).
    const QStringList delimCells = aligned[1].split(QLatin1Char('|'), Qt::SkipEmptyParts);
    ASSERT_EQ(delimCells.size(), 2);
    EXPECT_GE(delimCells[0].trimmed().size(), 4);
}

TEST(TableFormat, SkeletonShape) {
    const QString sk = TableFormat::skeleton(3, 2);
    const QStringList lines = sk.split(QLatin1Char('\n'));
    ASSERT_EQ(lines.size(), 4); // header + delimiter + 2 body rows
    EXPECT_TRUE(lines[0].contains(QStringLiteral("Col1")));
    EXPECT_TRUE(lines[0].contains(QStringLiteral("Col3")));
    // Placeholders must stay single words so a double-click selects one whole.
    for (const QString& cell : TableFormat::cells(lines[0])) {
        EXPECT_FALSE(cell.contains(QLatin1Char(' '))) << cell.toStdString();
    }
    EXPECT_TRUE(lines[1].contains(QLatin1Char('-')));
    for (const QString& line : lines) {
        EXPECT_TRUE(line.startsWith(QLatin1Char('|')));
        EXPECT_TRUE(line.endsWith(QLatin1Char('|')));
    }
}

// The header placeholders are single words so that a double-click selects one
// whole and it can be replaced by typing. "Header 1" selected only "Header",
// forcing the user to select the rest by hand just to rename a column.
TEST(TableFormat, SkeletonPlaceholdersAreOneWordEach) {
    const QString header = TableFormat::skeleton(3, 1).split(QLatin1Char('\n')).at(0);
    const QStringList cells = TableFormat::cells(header);
    ASSERT_EQ(cells.size(), 3);
    for (int c = 0; c < cells.size(); ++c) {
        EXPECT_EQ(cells.at(c), QStringLiteral("Col%1").arg(c + 1));
        EXPECT_FALSE(cells.at(c).contains(QLatin1Char(' '))) << cells.at(c).toStdString();
    }

    // Word selection from anywhere inside a placeholder covers all of it.
    QTextDocument doc;
    doc.setPlainText(header);
    const QTextBlock block = doc.findBlockByNumber(0);
    const int idx = header.indexOf(QStringLiteral("Col1"));
    ASSERT_GE(idx, 0);
    for (int within = 0; within < 4; ++within) {
        QTextCursor c(block);
        c.setPosition(block.position() + idx + within);
        c.select(QTextCursor::WordUnderCursor);
        EXPECT_EQ(c.selectedText(), QStringLiteral("Col1")) << "from offset " << within;
    }
}

TEST(TableFormat, SkeletonZeroBodyRows) {    const QString sk = TableFormat::skeleton(2, 0);
    const QStringList lines = sk.split(QLatin1Char('\n'));
    ASSERT_EQ(lines.size(), 2);
}

TEST(TableFormat, InsertAndDeleteRow) {
    const QStringList rows = {
        QStringLiteral("| a | b |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| 1 | 2 |"),
    };
    const QStringList added = TableFormat::insertRow(rows, 3);
    ASSERT_EQ(added.size(), 4);
    EXPECT_TRUE(added.last().startsWith(QLatin1Char('|')));

    const QStringList removed = TableFormat::deleteRow(added, 3);
    ASSERT_EQ(removed.size(), 3);
}

TEST(TableFormat, InsertAndDeleteColumn) {
    const QStringList rows = {
        QStringLiteral("| a | b |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| 1 | 2 |"),
    };
    const QStringList added = TableFormat::insertColumn(rows, 1);
    EXPECT_EQ(TableFormat::columnCount(added), 3);
    EXPECT_TRUE(added[0].contains(QStringLiteral("a")));
    EXPECT_TRUE(added[0].contains(QStringLiteral("b")));

    const QStringList removed = TableFormat::deleteColumn(added, 1);
    EXPECT_EQ(TableFormat::columnCount(removed), 2);
}

TEST(TableFormat, CellIndex) {
    const QString line = QStringLiteral("| aa | bb |");
    EXPECT_EQ(TableFormat::cellIndex(line, 0), 0);
    EXPECT_EQ(TableFormat::cellIndex(line, 3), 0);
    EXPECT_EQ(TableFormat::cellIndex(line, 7), 1);
}

TEST(TableFormat, AlignPreservesIntraCellLineBreak) {
    // U+2028 is whitespace per QChar::isSpace(), so a naive trimmed() call
    // silently deletes the user's Ctrl+Enter break during alignment.
    const QString cell = QStringLiteral("row 2") + QChar::LineSeparator + QStringLiteral("cont");
    const QStringList rows = {
        QStringLiteral("| Header 1 | Header 2 |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| row 1 | ") + cell + QStringLiteral(" |"),
    };
    const QStringList aligned = TableFormat::align(rows);
    ASSERT_EQ(aligned.size(), 3);
    EXPECT_TRUE(aligned[2].contains(QChar::LineSeparator));
    EXPECT_TRUE(aligned[2].contains(QStringLiteral("row 2")));
    EXPECT_TRUE(aligned[2].contains(QStringLiteral("cont")));

    // Idempotent: aligning again must not eat it either.
    EXPECT_EQ(TableFormat::align(aligned), aligned);

    // A break at the very end of a cell also survives.
    const QStringList trailing = {
        QStringLiteral("| a | b |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| x") + QChar::LineSeparator + QStringLiteral(" | y |"),
    };
    EXPECT_TRUE(TableFormat::align(trailing)[2].contains(QChar::LineSeparator));
}

TEST(TableFormat, CellContentEnd) {
    const QString line = QStringLiteral("| aa  | bb |");
    // Points just past the last non-padding character of the cell.
    EXPECT_EQ(line.mid(TableFormat::positionForCellOffset(line, 0, 0),
                       TableFormat::cellContentEnd(line, 0)
                           - TableFormat::positionForCellOffset(line, 0, 0)),
              QStringLiteral("aa"));
    EXPECT_EQ(line.mid(TableFormat::positionForCellOffset(line, 1, 0),
                       TableFormat::cellContentEnd(line, 1)
                           - TableFormat::positionForCellOffset(line, 1, 0)),
              QStringLiteral("bb"));

    // An empty cell collapses to a zero-width range (nothing to select).
    const QString empty = QStringLiteral("|     | b |");
    EXPECT_EQ(TableFormat::cellContentEnd(empty, 0), TableFormat::positionForCellOffset(empty, 0, 0));
}

TEST(TableFormat, EmptyCellPositionsAtLeftNotRight) {
    // An all-padding cell has no text, so the caret must go to the cell's left
    // edge. Skipping padding naively would run it to the closing pipe (far
    // right), which is where a click used to land.
    const QString line = QStringLiteral("| a | b |          |");
    const QVector<int> pipes = TableFormat::pipePositions(line);
    ASSERT_EQ(pipes.size(), 4);

    const int blankOpen = pipes[2];
    const int blankClose = pipes[3];
    const int pos = TableFormat::positionForCellOffset(line, 2, 0);

    // Just after the opening pipe, nowhere near the closing one.
    EXPECT_GT(pos, blankOpen);
    EXPECT_LE(pos, blankOpen + 2);
    EXPECT_LT(pos, blankClose - 1);

    // Zero-width range, so nothing gets selected in a blank cell.
    EXPECT_EQ(TableFormat::cellContentEnd(line, 2), pos);
}

TEST(TableFormat, EmptyCellInSkeletonPositionsAtLeft) {
    // The shape /table actually produces: header text plus blank body cells.
    const QStringList lines = TableFormat::skeleton(3, 1).split(QLatin1Char('\n'));
    ASSERT_EQ(lines.size(), 3);
    const QString bodyRow = lines[2];
    const QVector<int> pipes = TableFormat::pipePositions(bodyRow);
    ASSERT_EQ(pipes.size(), 4);

    for (int col = 0; col < 3; ++col) {
        const int pos = TableFormat::positionForCellOffset(bodyRow, col, 0);
        EXPECT_GT(pos, pipes[col]) << "col " << col;
        EXPECT_LE(pos, pipes[col] + 2) << "col " << col;
        // Must not drift to the right-hand edge of the cell.
        EXPECT_LT(pos, pipes[col + 1] - 1) << "col " << col;
    }
}

TEST(TableFormat, NonEmptyCellStillLandsOnFirstCharacter) {
    // Right-aligned padding sits *before* the text, so the caret should skip it.
    const QString line = QStringLiteral("|      42 | x |");
    const int pos = TableFormat::positionForCellOffset(line, 0, 0);
    EXPECT_EQ(line.mid(pos, 2), QStringLiteral("42"));
}

TEST(TableFormat, LineBreakRoundTrip) {
    const QString display = QStringLiteral("| a") + QChar::LineSeparator + QStringLiteral("b | c |");
    const QString md = TableFormat::toMarkdown(display);
    EXPECT_TRUE(md.contains(QStringLiteral("<br>")));
    EXPECT_FALSE(md.contains(QChar::LineSeparator));
    const QString back = TableFormat::toDisplay(md);
    EXPECT_TRUE(back.contains(QChar::LineSeparator));
    EXPECT_FALSE(back.contains(QStringLiteral("<br>")));
}
