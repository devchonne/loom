#include "markdown/DocumentOutline.h"

#include <QStringList>
#include <gtest/gtest.h>

TEST(DocumentOutline, SlugifyBasicPunctuation) {
    EXPECT_EQ(DocumentOutline::slugify(QStringLiteral("Hello, World!")), QStringLiteral("hello-world"));
    EXPECT_EQ(DocumentOutline::slugify(QStringLiteral("  Spaced   Out  ")), QStringLiteral("spaced-out"));
    EXPECT_EQ(DocumentOutline::slugify(QStringLiteral("C++ & Qt")), QStringLiteral("c-qt"));
}

TEST(DocumentOutline, SlugifyStripsInlineMarkup) {
    EXPECT_EQ(DocumentOutline::slugify(QStringLiteral("**Bold** and *em*")), QStringLiteral("bold-and-em"));
    EXPECT_EQ(DocumentOutline::slugify(QStringLiteral("`code` span")), QStringLiteral("code-span"));
    EXPECT_EQ(DocumentOutline::slugify(QStringLiteral("[a link](http://x)")), QStringLiteral("a-link"));
}

TEST(DocumentOutline, BuildDedupesSlugs) {
    const QStringList lines = {
        QStringLiteral("# Install"),
        QStringLiteral("body"),
        QStringLiteral("# Install"),
    };
    const auto entries = DocumentOutline::build(lines);
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0].slug, QStringLiteral("install"));
    EXPECT_EQ(entries[1].slug, QStringLiteral("install-1"));
}

TEST(DocumentOutline, BuildHonoursCustomId) {
    const QStringList lines = {QStringLiteral("## Arch Linux {#arch}")};
    const auto entries = DocumentOutline::build(lines);
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].slug, QStringLiteral("arch"));
    EXPECT_EQ(entries[0].text, QStringLiteral("Arch Linux"));
    EXPECT_EQ(entries[0].level, 2);
}

TEST(DocumentOutline, BuildSkipsFencedHeadings) {
    const QStringList lines = {
        QStringLiteral("# Real"),
        QStringLiteral("```"),
        QStringLiteral("# not a heading"),
        QStringLiteral("```"),
        QStringLiteral("## Also Real"),
    };
    const auto entries = DocumentOutline::build(lines);
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0].text, QStringLiteral("Real"));
    EXPECT_EQ(entries[1].text, QStringLiteral("Also Real"));
}

TEST(DocumentOutline, BuildSkipsSingleLineFencedHeadings) {
    const QStringList lines = {
        QStringLiteral("``` # not a heading ```"),
        QStringLiteral("# Real"),
    };
    const auto entries = DocumentOutline::build(lines);
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].text, QStringLiteral("Real"));
}

TEST(DocumentOutline, BuildRecordsBlockNumbers) {
    const QStringList lines = {
        QStringLiteral("intro"),
        QStringLiteral("# Heading"),
    };
    const auto entries = DocumentOutline::build(lines);
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].blockNumber, 1);
}

TEST(DocumentOutline, TocMarkdownNestsRelativeToShallowest) {
    QVector<OutlineEntry> entries;
    entries.append({0, 2, QStringLiteral("Install"), QStringLiteral("install")});
    entries.append({1, 3, QStringLiteral("Arch"), QStringLiteral("arch")});
    entries.append({2, 2, QStringLiteral("Usage"), QStringLiteral("usage")});

    const QString toc = DocumentOutline::tocMarkdown(entries);
    const QString expected = QStringLiteral("- [Install](#install)\n"
                                             "  - [Arch](#arch)\n"
                                             "- [Usage](#usage)");
    EXPECT_EQ(toc, expected);
}

TEST(DocumentOutline, TocMarkdownRespectsMaxLevel) {
    QVector<OutlineEntry> entries;
    entries.append({0, 1, QStringLiteral("Top"), QStringLiteral("top")});
    entries.append({1, 2, QStringLiteral("Sub"), QStringLiteral("sub")});
    entries.append({2, 3, QStringLiteral("Deep"), QStringLiteral("deep")});

    const QString toc = DocumentOutline::tocMarkdown(entries, 2);
    const QString expected = QStringLiteral("- [Top](#top)\n"
                                             "  - [Sub](#sub)");
    EXPECT_EQ(toc, expected);
}
