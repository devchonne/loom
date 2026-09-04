#include "ui/CheatSheet.h"

#include <gtest/gtest.h>

TEST(CheatSheetCatalog, HasEntriesInEverySection) {
    const QStringList sections = ShortcutCatalog::sections();
    EXPECT_FALSE(sections.isEmpty());
    EXPECT_FALSE(sections.contains(ShortcutCatalog::allSection()));
    for (const QString& section : sections) {
        EXPECT_GT(ShortcutCatalog::count(QString(), section), 0) << section.toStdString();
    }
}

TEST(CheatSheetCatalog, EmptyQueryReturnsEverything) {
    EXPECT_EQ(ShortcutCatalog::search(QString()).size(), ShortcutCatalog::entries().size());
    EXPECT_EQ(ShortcutCatalog::search(QStringLiteral("   ")).size(),
              ShortcutCatalog::entries().size());
}

TEST(CheatSheetCatalog, SectionFilterIsSubsetOfGlobal) {
    const int global = ShortcutCatalog::count(QString());
    int summed = 0;
    for (const QString& section : ShortcutCatalog::sections()) {
        summed += ShortcutCatalog::count(QString(), section);
    }
    EXPECT_EQ(summed, global);
}

TEST(CheatSheetCatalog, SearchIsGlobalAcrossSections) {
    const QVector<ShortcutEntry> hits = ShortcutCatalog::search(QStringLiteral("table"));
    ASSERT_FALSE(hits.isEmpty());
    QStringList seen;
    for (const ShortcutEntry& entry : hits) {
        if (!seen.contains(entry.section)) {
            seen.append(entry.section);
        }
    }
    // "table" lives in both the tables tab and the slash-command tab.
    EXPECT_GE(seen.size(), 2);
    EXPECT_TRUE(seen.contains(QStringLiteral("tables")));
    EXPECT_TRUE(seen.contains(QStringLiteral("slash")));
}

TEST(CheatSheetCatalog, SearchMatchesKeysActionsAndSectionNames) {
    EXPECT_GT(ShortcutCatalog::count(QStringLiteral("ctrl+shift")), 0);
    EXPECT_GT(ShortcutCatalog::count(QStringLiteral("zen")), 0);
    EXPECT_GT(ShortcutCatalog::count(QStringLiteral("slash")), 0);
    EXPECT_EQ(ShortcutCatalog::count(QStringLiteral("definitely-not-a-shortcut")), 0);
}

TEST(CheatSheetCatalog, SearchIsCaseInsensitiveAndTokenised) {
    const QVector<ShortcutEntry> hits = ShortcutCatalog::search(QStringLiteral("TABLE ROW"));
    ASSERT_FALSE(hits.isEmpty());
    for (const ShortcutEntry& entry : hits) {
        const QString haystack = (entry.keys + entry.action + entry.section).toLower();
        EXPECT_TRUE(haystack.contains(QStringLiteral("table")));
        EXPECT_TRUE(haystack.contains(QStringLiteral("row")));
    }
}

TEST(CheatSheetCatalog, SectionScopedSearchNarrowsResults) {
    const int global = ShortcutCatalog::count(QStringLiteral("table"));
    const int inTables = ShortcutCatalog::count(QStringLiteral("table"), QStringLiteral("tables"));
    EXPECT_GT(inTables, 0);
    EXPECT_LT(inTables, global);
    for (const ShortcutEntry& entry :
         ShortcutCatalog::search(QStringLiteral("table"), QStringLiteral("tables"))) {
        EXPECT_EQ(entry.section, QStringLiteral("tables"));
    }
}

TEST(CheatSheetCatalog, EntriesAreGroupedBySectionInOrder) {
    QStringList order;
    for (const ShortcutEntry& entry : ShortcutCatalog::entries()) {
        if (order.isEmpty() || order.last() != entry.section) {
            EXPECT_FALSE(order.contains(entry.section)) << entry.section.toStdString();
            order.append(entry.section);
        }
    }
    EXPECT_EQ(order, ShortcutCatalog::sections());
}

TEST(CheatSheetCatalog, NoBlankOrDuplicateRows) {
    QStringList seen;
    for (const ShortcutEntry& entry : ShortcutCatalog::entries()) {
        EXPECT_FALSE(entry.keys.trimmed().isEmpty());
        EXPECT_FALSE(entry.action.trimmed().isEmpty());
        const QString id = entry.section + QLatin1Char('|') + entry.keys;
        EXPECT_FALSE(seen.contains(id)) << id.toStdString();
        seen.append(id);
    }
}
