#include "core/Buffer.h"

#include <QTextCursor>
#include <gtest/gtest.h>

namespace {

void type(Buffer& buffer, const QString& text) {
    QTextCursor cursor(buffer.document());
    cursor.setPosition(qMax(0, buffer.document()->characterCount() - 1));
    cursor.insertText(text);
    buffer.captureHistory(cursor.position());
}

} // namespace

TEST(EditHistory, EachKeystrokeIsOneUndo) {
    Buffer buffer;
    type(buffer, QStringLiteral("h"));
    type(buffer, QStringLiteral("i"));
    EXPECT_EQ(buffer.text(), QStringLiteral("hi"));

    ASSERT_TRUE(buffer.undo());
    EXPECT_EQ(buffer.text(), QStringLiteral("h"));
    ASSERT_TRUE(buffer.undo());
    EXPECT_EQ(buffer.text(), QString());
    EXPECT_FALSE(buffer.canUndo());
    ASSERT_TRUE(buffer.canRedo());

    ASSERT_TRUE(buffer.redo());
    EXPECT_EQ(buffer.text(), QStringLiteral("h"));
    ASSERT_TRUE(buffer.redo());
    EXPECT_EQ(buffer.text(), QStringLiteral("hi"));
    EXPECT_FALSE(buffer.canRedo());
}

TEST(EditHistory, NewEditDropsRedo) {
    Buffer buffer;
    type(buffer, QStringLiteral("a"));
    type(buffer, QStringLiteral("b"));
    ASSERT_TRUE(buffer.undo());
    EXPECT_TRUE(buffer.canRedo());
    type(buffer, QStringLiteral("c"));
    EXPECT_FALSE(buffer.canRedo());
    EXPECT_EQ(buffer.text(), QStringLiteral("ac"));
}
