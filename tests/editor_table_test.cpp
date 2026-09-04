#include "core/Buffer.h"
#include "core/Settings.h"
#include "markdown/MarkdownHighlighter.h"
#include "markdown/TableFormat.h"
#include "ui/Editor.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QVector>
#include <gtest/gtest.h>

namespace {

// Canonical, already-aligned 3-column table with a single body row.
QString tableText() {
    const QStringList rows = {
        QStringLiteral("| Header 1 | Header 2 | Header 3 |"),
        QStringLiteral("| --- | --- | --- |"),
        QStringLiteral("| row 1 | row 2 | row 3 |"),
    };
    return TableFormat::align(rows).join(QLatin1Char('\n'));
}

void sendChar(Editor& editor, QChar c) {
    QKeyEvent press(QEvent::KeyPress, c.unicode(), Qt::NoModifier, QString(c));
    QCoreApplication::sendEvent(&editor, &press);
}

void typeText(Editor& editor, const QString& text) {
    for (const QChar& c : text) {
        sendChar(editor, c);
    }
}

void sendKey(Editor& editor, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(&editor, &press);
}

// A real double click, delivered the way Qt does it: through the viewport, as a
// press/release/dblclick/release sequence. Synthesising only the DblClick event
// (or sending to the widget instead of its viewport) produces no selection at
// all, so it would silently test nothing.
void doubleClickAt(Editor& editor, const QPoint& pos) {
    QWidget* vp = editor.viewport();
    const QPoint global = vp->mapToGlobal(pos);
    const struct {
        QEvent::Type type;
        Qt::MouseButtons buttons;
    } steps[] = {
        {QEvent::MouseButtonPress, Qt::LeftButton},
        {QEvent::MouseButtonRelease, Qt::NoButton},
        {QEvent::MouseButtonDblClick, Qt::LeftButton},
        {QEvent::MouseButtonRelease, Qt::NoButton},
    };
    for (const auto& step : steps) {
        QMouseEvent event(step.type, pos, global, Qt::LeftButton, step.buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(vp, &event);
    }
}

// Double click on the character at `offsetInBlock` of the given block.
void doubleClickOnChar(Editor& editor, int blockNumber, int offsetInBlock) {
    const QTextBlock block = editor.document()->findBlockByNumber(blockNumber);
    ASSERT_TRUE(block.isValid());
    QTextCursor aim(block);
    aim.setPosition(block.position() + offsetInBlock);
    doubleClickAt(editor, editor.cursorRect(aim).center());
}

void placeCaretAfter(Editor& editor, int blockNumber, const QString& needle) {
    const QTextBlock block = editor.document()->findBlockByNumber(blockNumber);
    ASSERT_TRUE(block.isValid());
    const int idx = block.text().indexOf(needle);
    ASSERT_GE(idx, 0);
    QTextCursor c(block);
    c.setPosition(block.position() + idx + needle.size());
    editor.setTextCursor(c);
}

int caretColumn(const Editor& editor) {
    const QTextCursor c = editor.textCursor();
    return TableFormat::cellIndex(c.block().text(), c.positionInBlock());
}

QString blockText(const QTextDocument& doc, int n) {
    return doc.findBlockByNumber(n).text();
}

// Mirrors Editor's own delimiter check: a row of only dashes/colons.
bool isTableDelimiterLineForTest(const QString& text) {
    for (const QString& cell : TableFormat::cells(text)) {
        static const QRegularExpression re(QStringLiteral(R"(^:?-+:?$)"));
        if (!re.match(cell.trimmed()).hasMatch()) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(EditorTable, TypingInHeaderStaysInSameCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());
    ASSERT_EQ(doc.blockCount(), 3);

    placeCaretAfter(editor, 0, QStringLiteral("Header 1"));
    typeText(editor, QStringLiteral("XYZ"));

    // The table must not gain or lose rows, and every character must land in
    // the header cell the caret started in.
    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_TRUE(blockText(doc, 0).contains(QStringLiteral("Header 1XYZ")))
        << blockText(doc, 0).toStdString();
    EXPECT_EQ(editor.textCursor().blockNumber(), 0);
    EXPECT_EQ(caretColumn(editor), 0);
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 1")));
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 3")));
}

TEST(EditorTable, TypingInBodyRowStaysInSameCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 2"));
    typeText(editor, QStringLiteral("abc"));

    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 2abc")))
        << blockText(doc, 2).toStdString();
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(caretColumn(editor), 1);
}

TEST(EditorTable, TypingAtRowStartStaysInsideFirstCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    // Caret parked before the (invisible) leading pipe, as a click on the far
    // left of the row would do.
    const QTextBlock row = doc.findBlockByNumber(2);
    QTextCursor c(row);
    c.setPosition(row.position());
    editor.setTextCursor(c);
    typeText(editor, QStringLiteral("zz"));

    EXPECT_EQ(doc.blockCount(), 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(blockText(doc, i).trimmed().startsWith(QLatin1Char('|')))
            << i << ": " << blockText(doc, i).toStdString();
    }
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("zzrow 1")))
        << blockText(doc, 2).toStdString();
}

TEST(EditorTable, ColumnsGrowTogether) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 0, QStringLiteral("Header 1"));
    typeText(editor, QStringLiteral("wide"));

    // All three lines stay the same display width after a live realign.
    const int w0 = blockText(doc, 0).size();
    EXPECT_EQ(blockText(doc, 1).size(), w0);
    EXPECT_EQ(blockText(doc, 2).size(), w0);
}

TEST(EditorTable, TabMovesToNextColumn) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 0, QStringLiteral("Header 1"));
    EXPECT_EQ(caretColumn(editor), 0);

    sendKey(editor, Qt::Key_Tab);
    EXPECT_EQ(editor.textCursor().blockNumber(), 0);
    EXPECT_EQ(caretColumn(editor), 1);

    sendKey(editor, Qt::Key_Tab);
    EXPECT_EQ(editor.textCursor().blockNumber(), 0);
    EXPECT_EQ(caretColumn(editor), 2);

    // No stray tab characters may be inserted into the table.
    EXPECT_FALSE(blockText(doc, 0).contains(QLatin1Char('\t')));
    EXPECT_EQ(doc.blockCount(), 3);
}

TEST(EditorTable, TabAtRowEndWrapsToNextRowSkippingDelimiter) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 0, QStringLiteral("Header 3"));
    sendKey(editor, Qt::Key_Tab);

    // Wraps past the delimiter row into the first body row, column 0.
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(caretColumn(editor), 0);
}

TEST(EditorTable, BacktabMovesToPreviousColumn) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 3"));
    EXPECT_EQ(caretColumn(editor), 2);

    sendKey(editor, Qt::Key_Backtab, Qt::ShiftModifier);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(caretColumn(editor), 1);

    // Wrapping backwards off the row start lands on the header, last column.
    sendKey(editor, Qt::Key_Backtab, Qt::ShiftModifier);
    sendKey(editor, Qt::Key_Backtab, Qt::ShiftModifier);
    EXPECT_EQ(editor.textCursor().blockNumber(), 0);
    EXPECT_EQ(caretColumn(editor), 2);
}

TEST(EditorTable, TabAtLastCellAppendsRow) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 3"));
    sendKey(editor, Qt::Key_Tab);

    EXPECT_EQ(doc.blockCount(), 4);
    EXPECT_EQ(editor.textCursor().blockNumber(), 3);
    EXPECT_EQ(caretColumn(editor), 0);
    EXPECT_TRUE(blockText(doc, 3).trimmed().startsWith(QLatin1Char('|')));
}

TEST(EditorTable, TypingAfterTabLandsInTargetCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 1"));
    sendKey(editor, Qt::Key_Tab);
    typeText(editor, QStringLiteral("hi"));

    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    // Tab lands at the start of the cell, so typing prepends to what's there.
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("hirow 2")))
        << blockText(doc, 2).toStdString();
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 1")));
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 3")));
}

TEST(EditorTable, TabPlacesCaretAtFirstCharOfCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 0, QStringLiteral("Header 1"));
    sendKey(editor, Qt::Key_Tab);

    // No selection, and the caret sits exactly on the 'H' of "Header 2".
    EXPECT_FALSE(editor.textCursor().hasSelection());
    const QString line = blockText(doc, 0);
    EXPECT_EQ(editor.textCursor().positionInBlock(), line.indexOf(QStringLiteral("Header 2")));
}

TEST(EditorTable, ClickInCellPaddingSnapsToCellText) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    // Ragged widths mean the aligned table has real padding inside cells.
    doc.setPlainText(TableFormat::align({QStringLiteral("| a | bbbbbbbb |"),
                                         QStringLiteral("| --- | --- |"),
                                         QStringLiteral("| x | y |")})
                         .join(QLatin1Char('\n')));

    const QTextBlock row = doc.findBlockByNumber(2);
    const QString line = row.text();
    const int yIdx = line.indexOf(QLatin1Char('y'));
    ASSERT_GT(yIdx, 0);

    // Drop the caret deep in the trailing padding after "y", as a click on the
    // right-hand side of that cell would.
    QTextCursor c(row);
    c.setPosition(row.position() + line.size() - 1);
    editor.setTextCursor(c);
    editor.snapCaretForTest();

    // Clamped back to just after "y", not left in the padding or on a pipe.
    EXPECT_EQ(editor.textCursor().positionInBlock(), yIdx + 1);
}

TEST(EditorTable, ClickBeforeLeadingPipeSnapsIntoFirstCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    const QTextBlock row = doc.findBlockByNumber(2);
    QTextCursor c(row);
    c.setPosition(row.position());
    editor.setTextCursor(c);
    editor.snapCaretForTest();

    // Caret moves past the invisible leading pipe onto the first character.
    EXPECT_EQ(editor.textCursor().positionInBlock(),
              row.text().indexOf(QStringLiteral("row 1")));
}

TEST(EditorTable, EnterAddsRowThenExitsOnEmptyLastRow) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 1"));
    sendKey(editor, Qt::Key_Return);
    ASSERT_EQ(doc.blockCount(), 4);
    ASSERT_EQ(editor.textCursor().blockNumber(), 3);

    // Enter again on the still-empty new row should leave the table instead of
    // trapping the caret in an endless run of blank rows.
    sendKey(editor, Qt::Key_Return);
    EXPECT_EQ(doc.blockCount(), 4);
    const int caretBlock = editor.textCursor().blockNumber();
    EXPECT_EQ(caretBlock, 3);
    EXPECT_FALSE(blockText(doc, caretBlock).contains(QLatin1Char('|')))
        << blockText(doc, caretBlock).toStdString();
}

TEST(EditorTable, ProseWithPipeBelowTableIsNotSwallowed) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText() + QStringLiteral("\nprose a | b tail"));
    ASSERT_EQ(doc.blockCount(), 4);

    placeCaretAfter(editor, 0, QStringLiteral("Header 1"));
    typeText(editor, QStringLiteral("Q"));

    // The paragraph underneath merely contains a pipe; it must not be pulled
    // into the table and reformatted as a row.
    EXPECT_EQ(blockText(doc, 3), QStringLiteral("prose a | b tail"));
    EXPECT_EQ(doc.blockCount(), 4);
}

TEST(EditorTable, LoadingDocumentDoesNotReformatProse) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    // A bulk set fires textChanged while the caret is still at position 0; the
    // realign must not walk out of the table and rewrite prose as table rows.
    doc.setPlainText(tableText() + QStringLiteral("\nprose a | b tail\n\n| x | y | z |"));
    EXPECT_EQ(blockText(doc, 3), QStringLiteral("prose a | b tail"));
    // The second, separate table keeps its own identity (it may be aligned, but
    // it must still be a standalone 3-column row, not merged with the first).
    EXPECT_TRUE(blockText(doc, 5).trimmed().startsWith(QLatin1Char('|')));
    EXPECT_EQ(TableFormat::columnCount({blockText(doc, 5)}), 3);
    EXPECT_EQ(doc.blockCount(), 6);
}

TEST(EditorTable, TypingInProseWithPipeIsUntouched) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(QStringLiteral("shell: cat a | wc -l"));

    QTextCursor c(doc.findBlockByNumber(0));
    c.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(c);
    typeText(editor, QStringLiteral("!"));

    // No table anywhere, so no snapping and no alignment.
    EXPECT_EQ(blockText(doc, 0), QStringLiteral("shell: cat a | wc -l!"));
    EXPECT_EQ(doc.blockCount(), 1);
}

TEST(EditorTable, TabOutsideTableDoesNotNavigateCells) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(QStringLiteral("plain paragraph"));

    QTextCursor c(doc.findBlockByNumber(0));
    c.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(c);
    sendKey(editor, Qt::Key_Tab);

    // Tab must not be hijacked by the table logic: no cell navigation, no new
    // rows, no pipes injected into ordinary prose.
    EXPECT_EQ(doc.blockCount(), 1);
    EXPECT_EQ(editor.textCursor().blockNumber(), 0);
    EXPECT_FALSE(blockText(doc, 0).contains(QLatin1Char('|')));
    EXPECT_TRUE(blockText(doc, 0).startsWith(QStringLiteral("plain paragraph")));
}

TEST(EditorTable, DeleteRowRefusesToRemoveHeader) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 0, QStringLiteral("Header 1"));
    EXPECT_FALSE(editor.deleteTableRow());
    EXPECT_EQ(doc.blockCount(), 3);

    // Deleting a genuine body row still works.
    placeCaretAfter(editor, 2, QStringLiteral("row 1"));
    EXPECT_TRUE(editor.deleteTableRow());
    EXPECT_EQ(doc.blockCount(), 2);
}

TEST(EditorTable, RepeatedTypingKeepsRowCountStable) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    // The original report was that the 2nd/3rd keystroke escaped the cell, so
    // hammer a cell and assert the structure never drifts.
    placeCaretAfter(editor, 2, QStringLiteral("row 2"));
    typeText(editor, QStringLiteral("0123456789"));

    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(caretColumn(editor), 1);
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 20123456789")))
        << blockText(doc, 2).toStdString();
    EXPECT_TRUE(blockText(doc, 0).contains(QStringLiteral("Header 1")));
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 3")));
}

TEST(EditorTable, CtrlEnterInsertsRealLineBreakInsideCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 2"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);

    // Must stay one block: a hard break would split the row into a new table.
    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    // In-document form is U+2028, which the layout renders as a real second
    // line. A literal "<br>" would just sit there as visible text.
    EXPECT_TRUE(blockText(doc, 2).contains(QChar::LineSeparator))
        << blockText(doc, 2).toStdString();
    EXPECT_FALSE(blockText(doc, 2).contains(QStringLiteral("<br>")));

    // The row really occupies two visual lines.
    doc.documentLayout()->documentSize();
    const QTextBlock row = doc.findBlockByNumber(2);
    ASSERT_NE(row.layout(), nullptr);
    EXPECT_GE(row.layout()->lineCount(), 2);

    // Neighbouring rows are untouched and the table still has 3 columns.
    EXPECT_TRUE(blockText(doc, 0).contains(QStringLiteral("Header 1")));
    EXPECT_EQ(TableFormat::columnCount({blockText(doc, 2)}), 3);
}

TEST(EditorTable, CellLineBreakSavesAsBrAndReloads) {
    Buffer buffer;
    Editor editor;
    editor.bindDocument(buffer.document(), true);
    buffer.setText(tableText(), true);

    placeCaretAfter(editor, 2, QStringLiteral("row 2"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);

    // Saved markdown carries "<br>", never a raw U+2028.
    const QString saved = buffer.text();
    EXPECT_TRUE(saved.contains(QStringLiteral("<br>"))) << saved.toStdString();
    EXPECT_FALSE(saved.contains(QChar::LineSeparator));

    // Reloading turns it back into a real in-document line break.
    buffer.setText(saved, true);
    EXPECT_TRUE(buffer.document()->findBlockByNumber(2).text().contains(QChar::LineSeparator));
    EXPECT_EQ(buffer.document()->blockCount(), 3);
}

TEST(EditorTable, SkeletonInsertionLeavesCaretInFirstHeaderCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);

    // Mirrors MainWindow::insertTableSkeleton after the "/table" line has been
    // consumed: caret on an empty block, skeleton inserted, trailing block added.
    QTextCursor cursor(&doc);
    const int headerBlock = cursor.blockNumber();
    cursor.beginEditBlock();
    cursor.insertText(TableFormat::skeleton(3, 1));
    cursor.insertBlock();
    cursor.endEditBlock();

    // Before focusing, the caret is stranded on the trailing block below.
    ASSERT_NE(cursor.blockNumber(), headerBlock);

    ASSERT_TRUE(editor.focusTableCell(headerBlock, 0));
    EXPECT_EQ(editor.textCursor().blockNumber(), headerBlock);
    EXPECT_FALSE(editor.textCursor().hasSelection());

    // Sits on the first character of the first header cell, so typing edits it.
    const QString header = blockText(doc, headerBlock);
    EXPECT_EQ(editor.textCursor().positionInBlock(), header.indexOf(QStringLiteral("Col1")));

    typeText(editor, QStringLiteral("Z"));
    EXPECT_TRUE(blockText(doc, headerBlock).contains(QStringLiteral("ZCol1")))
        << blockText(doc, headerBlock).toStdString();
}

TEST(EditorTable, FocusTableCellRejectsNonTableBlock) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(QStringLiteral("just prose"));
    EXPECT_FALSE(editor.focusTableCell(0, 0));
    EXPECT_FALSE(editor.focusTableCell(99, 0));
}

TEST(EditorTable, ClickInEmptyCellGoesToLeftEdge) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    // Exactly what /table 3x1 produces: filled headers, blank body cells.
    doc.setPlainText(TableFormat::skeleton(3, 1));
    ASSERT_EQ(doc.blockCount(), 3);

    const QTextBlock row = doc.findBlockByNumber(2);
    const QVector<int> pipes = TableFormat::pipePositions(row.text());
    ASSERT_EQ(pipes.size(), 4);

    // Click near the right-hand side of the last (empty) cell.
    QTextCursor c(row);
    c.setPosition(row.position() + pipes[3] - 1);
    editor.setTextCursor(c);
    editor.snapCaretForTest();

    const int pos = editor.textCursor().positionInBlock();
    EXPECT_GT(pos, pipes[2]);
    EXPECT_LE(pos, pipes[2] + 2);
    EXPECT_LT(pos, pipes[3] - 1);

    // Typing lands at the left of the cell, not flush against the right pipe.
    typeText(editor, QStringLiteral("hi"));
    const QStringList cells = blockText(doc, 2).split(QLatin1Char('|'));
    ASSERT_GE(cells.size(), 4);
    EXPECT_EQ(cells[3].trimmed(), QStringLiteral("hi"));
    EXPECT_TRUE(cells[3].startsWith(QStringLiteral(" hi"))) << cells[3].toStdString();
}

TEST(EditorTable, TabIntoEmptyCellGoesToLeftEdge) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    // Start in the first (empty) body cell, Tab into the second.
    ASSERT_TRUE(editor.focusTableCell(2, 0));
    sendKey(editor, Qt::Key_Tab);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_FALSE(editor.textCursor().hasSelection());

    const QVector<int> pipes = TableFormat::pipePositions(blockText(doc, 2));
    const int pos = editor.textCursor().positionInBlock();
    EXPECT_GT(pos, pipes[1]);
    EXPECT_LE(pos, pipes[1] + 2);

    typeText(editor, QStringLiteral("ok"));
    const QStringList cells = blockText(doc, 2).split(QLatin1Char('|'));
    ASSERT_GE(cells.size(), 3);
    EXPECT_EQ(cells[2].trimmed(), QStringLiteral("ok"));
}

TEST(EditorTable, BackspaceStaysWithinCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    placeCaretAfter(editor, 2, QStringLiteral("row 2"));
    sendKey(editor, Qt::Key_Backspace);
    sendKey(editor, Qt::Key_Backspace);

    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(blockText(doc, i).trimmed().startsWith(QLatin1Char('|')));
        EXPECT_TRUE(blockText(doc, i).trimmed().endsWith(QLatin1Char('|')));
    }
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 1")));
    EXPECT_TRUE(blockText(doc, 2).contains(QStringLiteral("row 3")));
}

// Regression: typing a space then another character used to lose the space,
// because the caret was clamped back to the end of the cell's *text* -- landing
// before the space just typed, so the next character overwrote it. Words came
// out cramped ("foobarbaz"), and the loss only showed up on the next keystroke.
TEST(EditorTable, TypedSpacesInsideCellSurvive) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("foo bar baz"));

    const QStringList cells = blockText(doc, 2).split(QLatin1Char('|'));
    ASSERT_GE(cells.size(), 3);
    EXPECT_EQ(cells[1].trimmed(), QStringLiteral("foo bar baz"));

    // Tab away and confirm the spaces are not dropped by the realign either.
    sendKey(editor, Qt::Key_Tab);
    EXPECT_EQ(TableFormat::cellIndex(blockText(doc, 2), editor.textCursor().positionInBlock()), 1);
    const QStringList after = blockText(doc, 2).split(QLatin1Char('|'));
    ASSERT_GE(after.size(), 3);
    EXPECT_EQ(after[1].trimmed(), QStringLiteral("foo bar baz"));
}

// Regression: spaces mid-cell survive when the caret sits in the middle of the
// text rather than at its end.
TEST(EditorTable, SpaceTypedMidCellSplitsWords) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText());

    // "row 1" -> caret between "row" and " 1", then type a space plus a word.
    placeCaretAfter(editor, 2, QStringLiteral("row"));
    typeText(editor, QStringLiteral(" two"));

    const QStringList cells = blockText(doc, 2).split(QLatin1Char('|'));
    ASSERT_GE(cells.size(), 2);
    EXPECT_EQ(cells[1].trimmed(), QStringLiteral("row two 1"));
}

// Regression: Ctrl+Enter kept the caret in the right cell logically, but the
// continuation line rendered at the row's far-left edge (a U+2028 restarts at
// the block's margin), so it looked like the caret had jumped to column 1.
// Alignment now indents the continuation under its own column.
TEST(EditorTable, CtrlEnterContinuationStaysUnderItsColumn) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::align({QStringLiteral("| h1 | h2 | h3 |"),
                                         QStringLiteral("| --- | --- | --- |"),
                                         QStringLiteral("| a | b | ccc |")})
                         .join(QLatin1Char('\n')));

    // Park at the end of "ccc" (third column) so the break goes after the text.
    placeCaretAfter(editor, 2, QStringLiteral("ccc"));
    const int columnBefore =
        TableFormat::cellIndex(blockText(doc, 2), editor.textCursor().positionInBlock());
    ASSERT_EQ(columnBefore, 2);

    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);

    const QString line = blockText(doc, 2);
    // Still one block: the row must not be split in two.
    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_TRUE(line.contains(QChar::LineSeparator));
    // The caret stays in the column it started in.
    EXPECT_EQ(TableFormat::cellIndex(line, editor.textCursor().positionInBlock()), columnBefore);

    // ...and the continuation is visually under that column, not at the far left.
    doc.documentLayout()->documentSize();
    QTextLayout* layout = doc.findBlockByNumber(2).layout();
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->lineCount(), 2);
    const QTextLine first = layout->lineAt(0);
    const QTextLine second = layout->lineAt(1);
    const qreal caretX = second.cursorToX(editor.textCursor().positionInBlock());
    // Where the row's first column starts: the wrapped caret must sit well to
    // the right of it.
    const qreal firstColumnX = first.cursorToX(TableFormat::positionForCellOffset(line, 0, 0));
    EXPECT_GT(caretX, firstColumnX);

    // The indent is presentational only; saved markdown keeps just the <br>.
    const QString markdown = TableFormat::toMarkdown(line);
    EXPECT_TRUE(markdown.contains(QStringLiteral("ccc<br>")));
    EXPECT_FALSE(markdown.contains(QStringLiteral("<br>  ")));
}

// Regression: the presentational indent must not accumulate, nor inflate column
// widths, when a row containing a break is realigned repeatedly.
TEST(EditorTable, RepeatedAlignDoesNotGrowBreakIndent) {
    const QStringList rows = {
        QStringLiteral("| h1 | h2 |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| a | one%1two |").arg(QChar::LineSeparator),
    };
    const QStringList once = TableFormat::align(rows);
    const QStringList twice = TableFormat::align(once);
    EXPECT_EQ(twice, once);
    EXPECT_EQ(TableFormat::align(twice), once);
}

// Regression (reported): typing in row 1 / column 1 and pressing Ctrl+Enter
// dumped the caret into the *last* column, so the text that followed appeared in
// the wrong cell. Two separate faults: the break was inserted through a copy of
// the cursor which was then restored over the caret realignment had just placed
// (the copy re-expanded to the end of the rewritten row), and the cell-offset
// mapping counted the presentational indent as content, so each keystroke landed
// before the previous one and words came out scrambled.
TEST(EditorTable, CtrlEnterInFirstCellKeepsCaretInFirstCell) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("Hello, this is a"));
    ASSERT_EQ(caretColumn(editor), 0);

    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);

    // The caret stays in row 1 / column 1, on the new continuation line.
    EXPECT_EQ(doc.blockCount(), 3);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(caretColumn(editor), 0);

    typeText(editor, QStringLiteral("test"));
    EXPECT_EQ(caretColumn(editor), 0);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);

    // Both lines of text live in the first cell, in the order they were typed,
    // and the other two cells are still empty. The row is a grid, so the cell's
    // text is read out per visual line.
    const QStringList cells = TableFormat::cells(blockText(doc, 2));
    ASSERT_GE(cells.size(), 3);
    const QStringList lines = cells[0].split(QChar::LineSeparator);
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0].trimmed(), QStringLiteral("Hello, this is a"));
    EXPECT_EQ(lines[1].trimmed(), QStringLiteral("test"));
    EXPECT_TRUE(cells[1].trimmed().isEmpty()) << cells[1].toStdString();
    EXPECT_TRUE(cells[2].trimmed().isEmpty()) << cells[2].toStdString();

    // Saved markdown keeps just the <br>, with no presentational padding.
    const QString markdown = TableFormat::toMarkdown(blockText(doc, 2));
    EXPECT_TRUE(markdown.contains(QStringLiteral("Hello, this is a<br>test")))
        << markdown.toStdString();
}

// A row that wraps is laid out as a grid: one fully piped visual line per
// intra-cell break, so the painted column borders stay straight and -- the point
// of it -- every column still begins on the row's first line.
TEST(EditorTable, BrokenCellPadsEveryVisualLineToColumnWidth) {
    const QStringList aligned = TableFormat::align({
        QStringLiteral("| h1 | h2 |"),
        QStringLiteral("| --- | --- |"),
        QStringLiteral("| aaaaaaaa%1bb | x |").arg(QChar::LineSeparator),
    });
    ASSERT_EQ(aligned.size(), 3);

    const QStringList visual = aligned.at(2).split(QChar::LineSeparator);
    ASSERT_EQ(visual.size(), 2);

    // Both visual lines are complete rows: same width, same pipe columns as the
    // header above them.
    const QVector<int> headerPipes = TableFormat::pipePositions(aligned.at(0));
    ASSERT_EQ(headerPipes.size(), 3);
    for (const QString& v : visual) {
        EXPECT_EQ(v.size(), aligned.at(0).size()) << v.toStdString();
        EXPECT_EQ(TableFormat::pipePositions(v), headerPipes) << v.toStdString();
    }

    // The break's two halves live on their own line, and the second column keeps
    // its text on the *first* line.
    EXPECT_TRUE(visual[0].contains(QStringLiteral("aaaaaaaa")));
    EXPECT_TRUE(visual[0].contains(QLatin1Char('x')));
    EXPECT_TRUE(visual[1].contains(QStringLiteral("bb")));

    // ...and it is still idempotent: padding must not accumulate.
    EXPECT_EQ(TableFormat::align(aligned), aligned);

    // The grid is presentational; the saved row folds back into one "<br>" cell.
    const QString markdown = TableFormat::toMarkdown(aligned.join(QLatin1Char('\n')));
    EXPECT_TRUE(markdown.contains(QStringLiteral("aaaaaaaa<br>bb"))) << markdown.toStdString();
    EXPECT_FALSE(markdown.contains(QChar::LineSeparator));
}

// Round-trip: a caret's (cell line, offset in that line) pair maps back to the
// same document position it came from, on both sides of a break. This pair -- not
// a single flat offset -- is what survives a realign: trailing alignment padding
// is addressable (a space the user just typed has to keep its place), so padding
// positions and the next line's positions would otherwise compete for the same
// numbers, and a trailing space would read back as "one line further down".
TEST(EditorTable, CellPositionRoundTripsAcrossLineBreak) {
    const QString line = TableFormat::align({
                                                QStringLiteral("| h1 | h2 |"),
                                                QStringLiteral("| --- | --- |"),
                                                QStringLiteral("| aaaa%1bb | x |").arg(QChar::LineSeparator),
                                            })
                             .at(2);

    ASSERT_EQ(TableFormat::visualLineCount(line), 2) << line.toStdString();
    const QStringList lines = TableFormat::cellLines(line, 0);
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], QStringLiteral("aaaa"));
    EXPECT_EQ(lines[1], QStringLiteral("bb"));

    // Walk both of the cell's lines, including their trailing padding, and
    // confirm every position round-trips exactly.
    for (int l = 0; l < 2; ++l) {
        for (int off = 0; off <= lines.at(l).size(); ++off) {
            const int pos = TableFormat::positionForCellLine(line, 0, l, off);
            EXPECT_EQ(TableFormat::cellIndex(line, pos), 0) << "line " << l << " off " << off;
            EXPECT_EQ(TableFormat::cellLineAt(line, pos), l) << "line " << l << " off " << off;
            EXPECT_EQ(TableFormat::offsetInCellLineAt(line, pos), off) << "line " << l << " off " << off;
        }
    }

    // Offset 0 on the second line is the continuation's first character, not a
    // position buried in padding.
    const int afterBreak = TableFormat::positionForCellLine(line, 0, 1, 0);
    EXPECT_EQ(line.mid(afterBreak, 2), QStringLiteral("bb")) << afterBreak;
}

// Reported case 1: Ctrl+Enter at the end of a short cell appeared to insert a
// space instead of a break. The separator was typed in at the raw caret position,
// which sits in the cell's trailing alignment padding, so it landed among the
// padding rather than after the text.
TEST(EditorTable, CtrlEnterAfterShortTextBreaksImmediately) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("Hello"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);

    // One press is enough: the row really is two visual lines now.
    const QString line = blockText(doc, 2);
    EXPECT_EQ(TableFormat::visualLineCount(line), 2) << line.toStdString();
    EXPECT_EQ(doc.blockCount(), 3);

    // The caret is on the new line, still in column 0, and the text typed next
    // goes there -- not into the padding of the line above.
    EXPECT_EQ(caretColumn(editor), 0);
    EXPECT_EQ(TableFormat::visualLineIndex(line, editor.textCursor().positionInBlock()), 1);

    typeText(editor, QStringLiteral("World"));
    const QStringList cells = TableFormat::cells(blockText(doc, 2));
    ASSERT_GE(cells.size(), 1);
    EXPECT_EQ(cells[0], QStringLiteral("Hello") + QChar::LineSeparator + QStringLiteral("World"));
}

// Reported case 1.1: each Ctrl+Enter must add exactly one line. Previously a
// press was swallowed until the caret had chewed through the cell's padding, so
// a short cell needed as many presses as the column was wide.
TEST(EditorTable, EachCtrlEnterAddsExactlyOneLine) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    ASSERT_TRUE(editor.focusTableCell(0, 0));
    // "Col1" is 4 wide, so "Hi" leaves a couple of columns of padding behind it.
    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("Hi"));

    for (int expected = 2; expected <= 4; ++expected) {
        sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
        EXPECT_EQ(TableFormat::visualLineCount(blockText(doc, 2)), expected)
            << blockText(doc, 2).toStdString();
        EXPECT_EQ(caretColumn(editor), 0);
    }
    EXPECT_EQ(doc.blockCount(), 3);
}

// Reported case 2: Down out of the header landed on the delimiter row, which
// reveals its raw "| --- |" markup, and -- because the dashes are narrower than
// the header text -- put the caret past the row's last pipe, i.e. in a column
// that does not exist. Vertical movement now walks whole rows and skips it.
TEST(EditorTable, DownArrowSkipsDelimiterRow) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));
    ASSERT_EQ(doc.blockCount(), 3);

    ASSERT_TRUE(editor.focusTableCell(0, 0));
    sendKey(editor, Qt::Key_Down);

    // Straight to row 1, column 1 -- never onto the delimiter.
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_FALSE(isTableDelimiterLineForTest(blockText(doc, 2)));
    EXPECT_EQ(caretColumn(editor), 0);
    EXPECT_EQ(editor.textCursor().positionInBlock(),
              TableFormat::positionForCellOffset(blockText(doc, 2), 0, 0));

    // Up again returns to the header, same column, also skipping the delimiter.
    sendKey(editor, Qt::Key_Up);
    EXPECT_EQ(editor.textCursor().blockNumber(), 0);
    EXPECT_EQ(caretColumn(editor), 0);
}

// Down from the last row must still be able to leave the table, otherwise the
// caret is trapped.
TEST(EditorTable, DownArrowLeavesTableAtLastRow) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(tableText() + QStringLiteral("\nafter"));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    sendKey(editor, Qt::Key_Down);
    EXPECT_EQ(editor.textCursor().blockNumber(), 3);
    EXPECT_EQ(blockText(doc, 3), QStringLiteral("after"));
}

// Vertical movement inside a wrapped cell walks the cell's own lines before
// leaving the row.
TEST(EditorTable, DownArrowWalksWrappedCellLines) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 2));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("one"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
    typeText(editor, QStringLiteral("two"));

    // Back to the cell's first line, then down through its second, then on to
    // the next row.
    ASSERT_TRUE(editor.focusTableCell(2, 0));
    ASSERT_EQ(TableFormat::visualLineIndex(blockText(doc, 2), editor.textCursor().positionInBlock()), 0);

    sendKey(editor, Qt::Key_Down);
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(TableFormat::visualLineIndex(blockText(doc, 2), editor.textCursor().positionInBlock()), 1);

    sendKey(editor, Qt::Key_Down);
    EXPECT_EQ(editor.textCursor().blockNumber(), 3);
    EXPECT_EQ(caretColumn(editor), 0);
}

// Reported case 3: Tab out of a wrapped cell put the caret on the *continuation*
// line of the next column, so text typed there appeared on the wrong visual line.
// Every column now starts on the row's first line, so offset 0 is unambiguous.
TEST(EditorTable, TabOutOfWrappedCellLandsOnFirstLine) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("Hello World"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
    typeText(editor, QStringLiteral("What's up"));
    ASSERT_EQ(TableFormat::visualLineCount(blockText(doc, 2)), 2);

    sendKey(editor, Qt::Key_Tab);

    const QString line = blockText(doc, 2);
    const int pos = editor.textCursor().positionInBlock();
    EXPECT_EQ(caretColumn(editor), 1);
    // The first line of the row, not the continuation of the cell next door.
    EXPECT_EQ(TableFormat::visualLineIndex(line, pos), 0) << line.toStdString();
    EXPECT_EQ(pos, TableFormat::positionForCellOffset(line, 1, 0));

    typeText(editor, QStringLiteral("Z"));
    const QStringList cells = TableFormat::cells(blockText(doc, 2));
    ASSERT_GE(cells.size(), 2);
    EXPECT_EQ(cells[0], QStringLiteral("Hello World") + QChar::LineSeparator + QStringLiteral("What's up"));
    EXPECT_EQ(cells[1], QStringLiteral("Z"));
}

// Regression: typing a space in a cell on a wrapped row's first line split the
// text across lines ("one cell" became "one" / "cell"). The caret sat in that
// line's trailing alignment padding, and the flat-offset mapping counted padding
// as belonging to the *next* line, so the realign moved the caret down a line.
TEST(EditorTable, TypingSpacesOnWrappedRowStaysOnSameLine) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    // Make the row wrap via column 0, then fill column 1 on the first line.
    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("Hello World"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
    typeText(editor, QStringLiteral("What's up"));
    ASSERT_EQ(TableFormat::visualLineCount(blockText(doc, 2)), 2);

    sendKey(editor, Qt::Key_Tab);
    typeText(editor, QStringLiteral("one cell"));

    // The space must not have bumped the caret onto the continuation line.
    const QStringList lines = TableFormat::cellLines(blockText(doc, 2), 1);
    ASSERT_GE(lines.size(), 1);
    EXPECT_EQ(lines[0], QStringLiteral("one cell")) << blockText(doc, 2).toStdString();
    for (int i = 1; i < lines.size(); ++i) {
        EXPECT_TRUE(lines.at(i).isEmpty()) << lines.at(i).toStdString();
    }
    // ...and column 0 is untouched.
    EXPECT_EQ(TableFormat::cells(blockText(doc, 2)).value(0),
              QStringLiteral("Hello World") + QChar::LineSeparator + QStringLiteral("What's up"));
    EXPECT_EQ(doc.blockCount(), 3);
}

// Ctrl+Enter mid-text splits the cell's line at the caret, carrying the tail onto
// the new line rather than duplicating or dropping it.
TEST(EditorTable, CtrlEnterMidTextSplitsCellLine) {
    QTextDocument doc;
    Editor editor;
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(2, 1));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("HelloWorld"));

    // Park between "Hello" and "World".
    placeCaretAfter(editor, 2, QStringLiteral("Hello"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);

    EXPECT_EQ(TableFormat::cells(blockText(doc, 2)).value(0),
              QStringLiteral("Hello") + QChar::LineSeparator + QStringLiteral("World"));
    // Caret is at the start of the new line, so typing prepends to "World".
    EXPECT_EQ(TableFormat::cellLineAt(blockText(doc, 2), editor.textCursor().positionInBlock()), 1);
    EXPECT_EQ(TableFormat::offsetInCellLineAt(blockText(doc, 2), editor.textCursor().positionInBlock()), 0);
    typeText(editor, QStringLiteral("X"));
    EXPECT_EQ(TableFormat::cellLines(blockText(doc, 2), 0).value(1), QStringLiteral("XWorld"));
}

// Rows are separated by a horizontal rule so a second line inside a cell reads
// differently from a second row. The rule has to fall in the *gap* between two
// rows: blockBoundingRect().bottom() excludes the part of a line contributed by
// the line-height multiplier, so drawing there struck through the text.
TEST(EditorTable, RowRulesFallBetweenRowsNotThroughText) {
    QTextDocument doc;
    Editor editor;
    editor.resize(560, 320);
    Settings settings;
    editor.applySettings(settings);
    editor.setTheme(Theme::builtin());
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 2));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("one"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
    typeText(editor, QStringLiteral("two"));
    ASSERT_TRUE(editor.focusTableCell(3, 0));
    typeText(editor, QStringLiteral("next row"));

    doc.documentLayout()->documentSize();
    ASSERT_EQ(TableFormat::visualLineCount(blockText(doc, 2)), 2);

    // The gap between the wrapped row and the row below it is real: the last
    // visual line of one ends strictly above the first line of the other.
    auto blockBottom = [&](int n) {
        QTextCursor c(doc.findBlockByNumber(n));
        c.movePosition(QTextCursor::EndOfBlock);
        return editor.cursorRect(c).bottom();
    };
    auto blockTop = [&](int n) {
        const QTextBlock b = doc.findBlockByNumber(n);
        QTextCursor c(b);
        c.setPosition(b.position());
        return editor.cursorRect(c).top();
    };
    EXPECT_LT(blockBottom(2), blockTop(3));

    // A rule drawn midway through that gap clears the text on both sides, and
    // there is no such gap *inside* the wrapped row -- its two visual lines are
    // contiguous, so no rule can land between them.
    const QTextLayout* wrapped = doc.findBlockByNumber(2).layout();
    ASSERT_NE(wrapped, nullptr);
    ASSERT_EQ(wrapped->lineCount(), 2);
    const qreal innerGap = wrapped->lineAt(1).y() - (wrapped->lineAt(0).y() + wrapped->lineAt(0).height());
    EXPECT_LE(innerGap, blockTop(3) - blockBottom(2));
}

// Reported: double clicking near a cell's edge selected the invisible "|". Qt
// treats a pipe as a word of its own, so the selection highlight made it visible
// and the next keystroke replaced it -- breaking the row out of the table. The
// word selection is now clamped to the cell's own text.
TEST(EditorTable, DoubleClickNeverSelectsAPipe) {
    QTextDocument doc;
    Editor editor;
    editor.resize(420, 200);
    Settings settings;
    editor.applySettings(settings);
    editor.setTheme(Theme::builtin());
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 2));

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("asdas"));
    ASSERT_TRUE(editor.focusTableCell(2, 1));
    typeText(editor, QStringLiteral("bee"));

    // Sweep every position of the header and of a filled body row: a pipe must
    // never end up inside the selection, wherever the pointer lands.
    for (const int blockNo : {0, 2}) {
        const int len = blockText(doc, blockNo).size();
        for (int i = 0; i < len; ++i) {
            doubleClickOnChar(editor, blockNo, i);
            const QString sel = editor.textCursor().selectedText();
            EXPECT_FALSE(sel.contains(QLatin1Char('|')))
                << "block " << blockNo << " offset " << i << " selected [" << sel.toStdString() << "]";
        }
    }

    // The table itself is untouched by all that clicking.
    EXPECT_EQ(doc.blockCount(), 4);
    EXPECT_EQ(TableFormat::cells(blockText(doc, 2)).value(0), QStringLiteral("asdas"));
    EXPECT_EQ(TableFormat::cells(blockText(doc, 2)).value(1), QStringLiteral("bee"));
}

// Double clicking a word inside a cell still selects that whole word, so it can
// be replaced by typing -- the reason the placeholders are single words.
TEST(EditorTable, DoubleClickSelectsCellWordForRenaming) {
    QTextDocument doc;
    Editor editor;
    editor.resize(420, 200);
    Settings settings;
    editor.applySettings(settings);
    editor.setTheme(Theme::builtin());
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    const QString header = blockText(doc, 0);
    const int idx = header.indexOf(QStringLiteral("Col2"));
    ASSERT_GE(idx, 0);
    doubleClickOnChar(editor, 0, idx + 2);
    EXPECT_EQ(editor.textCursor().selectedText(), QStringLiteral("Col2"));

    // Typing over the selection renames the column and the table realigns.
    typeText(editor, QStringLiteral("Price"));
    const QStringList cells = TableFormat::cells(blockText(doc, 0));
    ASSERT_EQ(cells.size(), 3);
    EXPECT_EQ(cells[0], QStringLiteral("Col1"));
    EXPECT_EQ(cells[1], QStringLiteral("Price"));
    EXPECT_EQ(cells[2], QStringLiteral("Col3"));
    EXPECT_EQ(doc.blockCount(), 3);
}

// A double click in an empty cell leaves the caret in that cell rather than
// selecting the pipe beside it.
TEST(EditorTable, DoubleClickInEmptyCellStaysInCell) {
    QTextDocument doc;
    Editor editor;
    editor.resize(420, 200);
    Settings settings;
    editor.applySettings(settings);
    editor.setTheme(Theme::builtin());
    editor.bindDocument(&doc, true);
    doc.setPlainText(TableFormat::skeleton(3, 1));

    const QVector<int> pipes = TableFormat::pipePositions(blockText(doc, 2));
    ASSERT_EQ(pipes.size(), 4);
    // Middle of the second (empty) body cell.
    doubleClickOnChar(editor, 2, (pipes[1] + pipes[2]) / 2);

    EXPECT_FALSE(editor.textCursor().selectedText().contains(QLatin1Char('|')));
    EXPECT_EQ(editor.textCursor().blockNumber(), 2);
    EXPECT_EQ(caretColumn(editor), 1);

    // Typing lands in that cell and nowhere else.
    typeText(editor, QStringLiteral("x"));
    const QStringList cells = TableFormat::cells(blockText(doc, 2));
    ASSERT_EQ(cells.size(), 3);
    EXPECT_EQ(cells[1], QStringLiteral("x"));
    EXPECT_TRUE(cells[0].isEmpty());
    EXPECT_TRUE(cells[2].isEmpty());
}

// Double clicking prose that happens to contain a pipe keeps Qt's normal
// behaviour: the clamping must not leak out of tables.
TEST(EditorTable, DoubleClickInProseWithPipeIsUnchanged) {
    QTextDocument doc;
    Editor editor;
    editor.resize(420, 200);
    Settings settings;
    editor.applySettings(settings);
    editor.setTheme(Theme::builtin());
    editor.bindDocument(&doc, true);
    doc.setPlainText(QStringLiteral("shell: cat a | wc -l"));

    const int idx = blockText(doc, 0).indexOf(QStringLiteral("cat"));
    ASSERT_GE(idx, 0);
    doubleClickOnChar(editor, 0, idx + 1);
    EXPECT_EQ(editor.textCursor().selectedText(), QStringLiteral("cat"));
    EXPECT_EQ(blockText(doc, 0), QStringLiteral("shell: cat a | wc -l"));
}

// The on-screen grid must survive a save/load round trip as a single "<br>" row.
TEST(EditorTable, WrappedRowRoundTripsThroughBr) {
    Buffer buffer;
    Editor editor;
    editor.bindDocument(buffer.document(), true);
    buffer.setText(TableFormat::skeleton(3, 1), true);

    ASSERT_TRUE(editor.focusTableCell(2, 0));
    typeText(editor, QStringLiteral("one"));
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
    typeText(editor, QStringLiteral("two"));

    const QString saved = buffer.text();
    EXPECT_TRUE(saved.contains(QStringLiteral("one<br>two"))) << saved.toStdString();
    EXPECT_FALSE(saved.contains(QChar::LineSeparator));
    // Still three lines on disk: the grid is presentational only.
    EXPECT_EQ(saved.split(QLatin1Char('\n')).size(), 3) << saved.toStdString();

    // Reloading rebuilds the grid, so a loaded table navigates like an edited one.
    buffer.setText(saved, true);
    const QString reloaded = buffer.document()->findBlockByNumber(2).text();
    EXPECT_EQ(buffer.document()->blockCount(), 3);
    EXPECT_EQ(TableFormat::visualLineCount(reloaded), 2) << reloaded.toStdString();
    EXPECT_EQ(TableFormat::cells(reloaded).value(0),
              QStringLiteral("one") + QChar::LineSeparator + QStringLiteral("two"));
}
