#include "core/LineDiff.h"

#include <gtest/gtest.h>

TEST(LineDiff, Identical) {
    const QStringList lines{QStringLiteral("a"), QStringLiteral("b")};
    const auto d = diffLines(lines, lines);
    ASSERT_EQ(d.left.size(), 2);
    ASSERT_EQ(d.right.size(), 2);
    EXPECT_EQ(d.left[0], LineMark::Equal);
    EXPECT_EQ(d.left[1], LineMark::Equal);
    EXPECT_EQ(d.right[0], LineMark::Equal);
    EXPECT_EQ(d.right[1], LineMark::Equal);
}

TEST(LineDiff, InsertMiddle) {
    const auto d = diffLines(QStringList{QStringLiteral("a"), QStringLiteral("c")},
                             QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    ASSERT_EQ(d.left.size(), 2);
    ASSERT_EQ(d.right.size(), 3);
    EXPECT_EQ(d.left[0], LineMark::Equal);
    EXPECT_EQ(d.left[1], LineMark::Equal);
    EXPECT_EQ(d.right[0], LineMark::Equal);
    EXPECT_EQ(d.right[1], LineMark::Insert);
    EXPECT_EQ(d.right[2], LineMark::Equal);
}

TEST(LineDiff, DeleteMiddle) {
    const auto d = diffLines(QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                             QStringList{QStringLiteral("a"), QStringLiteral("c")});
    EXPECT_EQ(d.left[1], LineMark::Delete);
    EXPECT_EQ(d.right[0], LineMark::Equal);
    EXPECT_EQ(d.right[1], LineMark::Equal);
}

TEST(LineDiff, ReplaceLine) {
    const auto d = diffLines(QStringList{QStringLiteral("hello")}, QStringList{QStringLiteral("bye")});
    ASSERT_EQ(d.left.size(), 1);
    ASSERT_EQ(d.right.size(), 1);
    EXPECT_EQ(d.left[0], LineMark::Change);
    EXPECT_EQ(d.right[0], LineMark::Change);
}

TEST(LineDiff, EmptyVsContent) {
    const auto d = diffLines(QStringList{}, QStringList{QStringLiteral("x")});
    EXPECT_TRUE(d.left.isEmpty());
    ASSERT_EQ(d.right.size(), 1);
    EXPECT_EQ(d.right[0], LineMark::Insert);
}

TEST(LineDiff, PrefixThenTailInsert) {
    const auto d = diffLines(QStringList{QStringLiteral("keep")},
                             QStringList{QStringLiteral("keep"), QStringLiteral("new")});
    EXPECT_EQ(d.left[0], LineMark::Equal);
    EXPECT_EQ(d.right[0], LineMark::Equal);
    EXPECT_EQ(d.right[1], LineMark::Insert);
}
