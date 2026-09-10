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

// Ctrl+K is the only discovery surface loom has, so every vault shortcut has to
// be listed there or the feature is effectively invisible once enabled.
TEST(CheatSheetCatalog, VaultSectionListsEveryVaultShortcut) {
    const QStringList sections = ShortcutCatalog::sections();
    ASSERT_TRUE(sections.contains(QStringLiteral("vault")));

    const auto rows = ShortcutCatalog::search(QString(), QStringLiteral("vault"));
    QStringList keys;
    for (const ShortcutEntry& entry : rows) {
        keys << entry.keys;
    }
    // The four bound key sequences, exactly as wireShortcuts() registers them.
    for (const QString& key : {QStringLiteral("Ctrl+E"), QStringLiteral("Ctrl+Shift+E"),
                               QStringLiteral("Ctrl+Shift+B"), QStringLiteral("Ctrl+Shift+V")}) {
        EXPECT_TRUE(keys.contains(key))
            << key.toStdString() << " missing from the vault section: "
            << keys.join(QStringLiteral(", ")).toStdString();
    }
    // And the wikilink syntax, which is the other half of using a vault.
    EXPECT_GT(ShortcutCatalog::count(QStringLiteral("[[")), 0);
}

// The vault rows must also be reachable by the words someone would actually
// type, not just by their key chord.
TEST(CheatSheetCatalog, VaultRowsAreSearchableByName) {
    for (const QString& query : {QStringLiteral("vault"), QStringLiteral("tree"),
                                 QStringLiteral("backlinks"), QStringLiteral("note")}) {
        EXPECT_GT(ShortcutCatalog::count(query), 0) << query.toStdString();
    }
}

// Slash commands are documented in the slash tab, so /vault belongs there too.
TEST(CheatSheetCatalog, VaultSlashCommandsAreDocumented) {
    const auto rows = ShortcutCatalog::search(QStringLiteral("/vault"), QStringLiteral("slash"));
    EXPECT_GE(rows.size(), 5);
}

// A shortcut that uses a key the user finds awkward should not creep back in.
// Backslash chords in particular are hard to reach on many layouts, so the table
// alignment binding is the only one allowed to use it.
TEST(CheatSheetCatalog, VaultShortcutsAvoidBackslashChords) {
    for (const ShortcutEntry& entry : ShortcutCatalog::search(QString(), QStringLiteral("vault"))) {
        EXPECT_FALSE(entry.keys.contains(QLatin1Char('\\')))
            << entry.keys.toStdString() << " / " << entry.action.toStdString();
    }
}
