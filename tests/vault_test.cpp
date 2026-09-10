#include "core/Vault.h"
#include "core/VaultRegistry.h"
#include "core/Paths.h"
#include "markdown/MarkdownRules.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

void writeNote(const QString& path, const QString& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(body.toUtf8());
}

// A small vault:
//   index.md
//   daily/monday.md
//   daily/tuesday.md
//   projects/loom.md
//   projects/deep/loom.md   (same name, different folder)
QString makeVaultTree(const QTemporaryDir& dir) {
    const QString root = dir.path() + QStringLiteral("/vault");
    writeNote(root + QStringLiteral("/index.md"),
              QStringLiteral("# index\n\nsee [[projects/loom]] and [[monday]]\n"));
    writeNote(root + QStringLiteral("/daily/monday.md"),
              QStringLiteral("# monday\n\nback to [[index]]\n"));
    writeNote(root + QStringLiteral("/daily/tuesday.md"),
              QStringLiteral("# tuesday\n\nalso [[index|the index]]\n"));
    writeNote(root + QStringLiteral("/projects/loom.md"), QStringLiteral("# loom\n"));
    writeNote(root + QStringLiteral("/projects/deep/loom.md"), QStringLiteral("# loom deep\n"));
    // Ignored: not a note suffix, and a dotted folder.
    writeNote(root + QStringLiteral("/notes.pdf.bin"), QStringLiteral("binary"));
    writeNote(root + QStringLiteral("/.trash/gone.md"), QStringLiteral("# gone\n"));
    return root;
}

} // namespace

TEST(VaultPure, NormalizeNameIsCaseAndWhitespaceInsensitive) {
    EXPECT_EQ(Vault::normalizeName(QStringLiteral("  My   Note ")),
              Vault::normalizeName(QStringLiteral("my note")));
    EXPECT_NE(Vault::normalizeName(QStringLiteral("a")), Vault::normalizeName(QStringLiteral("b")));
}

TEST(VaultPure, SplitAnchorSeparatesFileFromHeading) {
    QString name;
    QString anchor;
    Vault::splitAnchor(QStringLiteral("Note#Some Heading"), &name, &anchor);
    EXPECT_EQ(name, QStringLiteral("Note"));
    EXPECT_EQ(anchor, QStringLiteral("Some Heading"));

    Vault::splitAnchor(QStringLiteral("Note"), &name, &anchor);
    EXPECT_EQ(name, QStringLiteral("Note"));
    EXPECT_TRUE(anchor.isEmpty());

    // A leading '#' is a same-document anchor and must be left intact.
    Vault::splitAnchor(QStringLiteral("#Heading"), &name, &anchor);
    EXPECT_EQ(name, QStringLiteral("#Heading"));
    EXPECT_TRUE(anchor.isEmpty());
}

TEST(VaultPure, IsNoteAndStripSuffix) {
    EXPECT_TRUE(Vault::isNote(QStringLiteral("/tmp/a.md")));
    EXPECT_TRUE(Vault::isNote(QStringLiteral("/tmp/a.MARKDOWN")));
    EXPECT_TRUE(Vault::isNote(QStringLiteral("/tmp/a.txt")));
    EXPECT_FALSE(Vault::isNote(QStringLiteral("/tmp/a.png")));
    EXPECT_EQ(Vault::stripSuffix(QStringLiteral("a.md")), QStringLiteral("a"));
    EXPECT_EQ(Vault::stripSuffix(QStringLiteral("a.png")), QStringLiteral("a.png"));
}

TEST(VaultPure, ResolveRelativeTriesBareTargetThenSuffixes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeNote(dir.path() + QStringLiteral("/sibling.md"), QStringLiteral("x"));

    EXPECT_EQ(Vault::resolveRelative(QStringLiteral("sibling"), dir.path()),
              dir.path() + QStringLiteral("/sibling.md"));
    EXPECT_EQ(Vault::resolveRelative(QStringLiteral("sibling.md"), dir.path()),
              dir.path() + QStringLiteral("/sibling.md"));
    EXPECT_TRUE(Vault::resolveRelative(QStringLiteral("missing"), dir.path()).isEmpty());
    // No base directory to resolve against.
    EXPECT_TRUE(Vault::resolveRelative(QStringLiteral("sibling"), QString()).isEmpty());
}

TEST(Vault, EmptyRootIsInert) {
    Vault vault;
    EXPECT_FALSE(vault.isOpen());
    EXPECT_EQ(vault.count(), 0);
    EXPECT_FALSE(vault.contains(QStringLiteral("/tmp/anything.md")));
    EXPECT_TRUE(vault.resolve(QStringLiteral("note"), QStringLiteral("/tmp/a.md"), true).isEmpty());
    EXPECT_TRUE(vault.backlinks(QStringLiteral("/tmp/a.md")).isEmpty());
}

TEST(Vault, IndexesNotesAndSkipsDottedFoldersAndOtherSuffixes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    ASSERT_TRUE(vault.isOpen());
    EXPECT_EQ(vault.name(), QStringLiteral("vault"));

    QStringList relatives;
    for (const VaultNote& note : vault.notes()) {
        relatives << note.relative;
    }
    EXPECT_EQ(vault.count(), 5) << relatives.join(QStringLiteral(", ")).toStdString();
    EXPECT_TRUE(relatives.contains(QStringLiteral("index.md")));
    EXPECT_TRUE(relatives.contains(QStringLiteral("daily/monday.md")));
    EXPECT_TRUE(relatives.contains(QStringLiteral("projects/deep/loom.md")));
    // .trash is skipped, and a non-note suffix is not indexed.
    EXPECT_FALSE(relatives.contains(QStringLiteral(".trash/gone.md")));
    for (const QString& relative : relatives) {
        EXPECT_FALSE(relative.endsWith(QStringLiteral(".bin")));
    }
}

TEST(Vault, ContainsDoesNotMatchSiblingPrefix) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);
    QDir().mkpath(root + QStringLiteral("-backup"));

    Vault vault;
    vault.setRoot(root);
    EXPECT_TRUE(vault.contains(root + QStringLiteral("/index.md")));
    EXPECT_TRUE(vault.contains(root + QStringLiteral("/daily/monday.md")));
    // "/vault-backup" must not count as being inside "/vault".
    EXPECT_FALSE(vault.contains(root + QStringLiteral("-backup/index.md")));
    EXPECT_FALSE(vault.contains(dir.path() + QStringLiteral("/outside.md")));
}

TEST(Vault, WikiTargetResolvesByNameAcrossTheTree) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    const QString index = root + QStringLiteral("/index.md");

    EXPECT_EQ(vault.resolve(QStringLiteral("monday"), index, true),
              root + QStringLiteral("/daily/monday.md"));
    // Case and spacing insensitive.
    EXPECT_EQ(vault.resolve(QStringLiteral("MONDAY"), index, true),
              root + QStringLiteral("/daily/monday.md"));
    // An explicit relative path also works.
    EXPECT_EQ(vault.resolve(QStringLiteral("projects/loom"), index, true),
              root + QStringLiteral("/projects/loom.md"));
    // An anchor does not change which file is opened.
    EXPECT_EQ(vault.resolve(QStringLiteral("monday#notes"), index, true),
              root + QStringLiteral("/daily/monday.md"));
    EXPECT_TRUE(vault.resolve(QStringLiteral("nope"), index, true).isEmpty());
}

TEST(Vault, AmbiguousWikiTargetPrefersTheSameFolderThenTheShallowest) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    // Two notes named "loom": from inside projects/deep the sibling wins.
    EXPECT_EQ(vault.resolve(QStringLiteral("loom"), root + QStringLiteral("/projects/deep/loom.md"),
                            true),
              root + QStringLiteral("/projects/deep/loom.md"));
    // From anywhere else the shallowest one wins, and the answer is stable.
    const QString fromIndex =
        vault.resolve(QStringLiteral("loom"), root + QStringLiteral("/index.md"), true);
    EXPECT_EQ(fromIndex, root + QStringLiteral("/projects/loom.md"));
    EXPECT_EQ(fromIndex,
              vault.resolve(QStringLiteral("loom"), root + QStringLiteral("/index.md"), true));
}

TEST(Vault, WikiTargetOutsideTheVaultFallsBackToRelativeResolution) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);
    // A document that is not in the vault, with a sibling of its own.
    const QString outside = dir.path() + QStringLiteral("/loose/notes.md");
    writeNote(outside, QStringLiteral("# notes\n"));
    writeNote(dir.path() + QStringLiteral("/loose/sibling.md"), QStringLiteral("# sibling\n"));

    Vault vault;
    vault.setRoot(root);
    // Name lookup must not reach into the vault for a document outside it.
    EXPECT_TRUE(vault.resolve(QStringLiteral("monday"), outside, true).isEmpty());
    // But the link is not dead: it resolves next to the document instead.
    EXPECT_EQ(vault.resolve(QStringLiteral("sibling"), outside, true),
              dir.path() + QStringLiteral("/loose/sibling.md"));
}

TEST(Vault, WikiLinksStillResolveWithNoVaultAtAll) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString from = dir.path() + QStringLiteral("/a.md");
    writeNote(from, QStringLiteral("# a\n"));
    writeNote(dir.path() + QStringLiteral("/b.md"), QStringLiteral("# b\n"));

    // This is the promise that makes the feature safe to switch off: a document
    // written inside a vault keeps working without one.
    Vault vault;
    EXPECT_FALSE(vault.isOpen());
    EXPECT_EQ(vault.resolve(QStringLiteral("b"), from, true), dir.path() + QStringLiteral("/b.md"));
    EXPECT_EQ(vault.resolve(QStringLiteral("b.md"), from, false),
              dir.path() + QStringLiteral("/b.md"));
}

TEST(Vault, PlainTargetsAreNeverLookedUpByName) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    const QString index = root + QStringLiteral("/index.md");
    // "monday" only exists in daily/, so a plain [text](monday) must not find it:
    // only [[monday]] gets name resolution.
    EXPECT_TRUE(vault.resolve(QStringLiteral("monday"), index, false).isEmpty());
    EXPECT_EQ(vault.resolve(QStringLiteral("daily/monday.md"), index, false),
              root + QStringLiteral("/daily/monday.md"));
}

TEST(Vault, NewNotePathLandsNextToTheReferringNote) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    EXPECT_EQ(vault.newNotePath(QStringLiteral("wednesday"),
                                root + QStringLiteral("/daily/monday.md")),
              root + QStringLiteral("/daily/wednesday.md"));
    // A slash is an explicit location, relative to the root.
    EXPECT_EQ(vault.newNotePath(QStringLiteral("projects/new"),
                                root + QStringLiteral("/daily/monday.md")),
              root + QStringLiteral("/projects/new.md"));
}

TEST(Vault, CreateNoteSeedsAHeadingAndNeverClobbers) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    QString err;
    const QString first = vault.createNote(root, QStringLiteral("fresh"), &err);
    ASSERT_FALSE(first.isEmpty()) << err.toStdString();
    EXPECT_EQ(first, root + QStringLiteral("/fresh.md"));

    QFile file(first);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_EQ(QString::fromUtf8(file.readAll()), QStringLiteral("# fresh\n\n"));

    // A second note with the same name is de-duplicated, not overwritten.
    const QString second = vault.createNote(root, QStringLiteral("fresh"), &err);
    EXPECT_EQ(second, root + QStringLiteral("/fresh 2.md"));
    EXPECT_TRUE(QFile::exists(first));
}

TEST(Vault, BacklinksFindWikiAndPathReferences) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);
    // A plain markdown link to the same note, and a decoy inside a code fence.
    writeNote(root + QStringLiteral("/other.md"),
              QStringLiteral("# other\n\nsee [the index](index.md)\n\n```\n[[index]]\n```\n"));

    Vault vault;
    vault.setRoot(root);
    const auto links = vault.backlinks(root + QStringLiteral("/index.md"));

    QStringList names;
    for (const VaultBacklink& link : links) {
        names << link.name;
        EXPECT_GT(link.line, 0);
        EXPECT_FALSE(link.context.isEmpty());
    }
    names.sort();
    // monday ([[index]]), tuesday ([[index|the index]]) and other ([](index.md)).
    // The fenced [[index]] in other.md must not add a second row.
    EXPECT_EQ(names, QStringList({QStringLiteral("monday"), QStringLiteral("other"),
                                  QStringLiteral("tuesday")}));
    // A note never backlinks to itself.
    for (const VaultBacklink& link : links) {
        EXPECT_NE(link.path, root + QStringLiteral("/index.md"));
    }
}

TEST(Vault, SwitchingRootDropsTheOldIndex) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);
    const QString other = dir.path() + QStringLiteral("/second");
    writeNote(other + QStringLiteral("/only.md"), QStringLiteral("# only\n"));

    Vault vault;
    vault.setRoot(root);
    EXPECT_EQ(vault.count(), 5);
    vault.setRoot(other);
    EXPECT_EQ(vault.count(), 1);
    EXPECT_FALSE(vault.contains(root + QStringLiteral("/index.md")));
    // And closing it entirely leaves nothing behind.
    vault.setRoot(QString());
    EXPECT_FALSE(vault.isOpen());
    EXPECT_EQ(vault.count(), 0);
}

TEST(VaultRegistryTest, RoundTripAndMostRecentFirst) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());
    qputenv("LOOM_CONFIG_DIR", (dir.path() + QStringLiteral("/config")).toUtf8());
    const QString a = dir.path() + QStringLiteral("/alpha");
    const QString b = dir.path() + QStringLiteral("/beta");
    QDir().mkpath(a);
    QDir().mkpath(b);

    VaultRegistry registry;
    registry.remember(a);
    registry.remember(b);
    ASSERT_EQ(registry.count(), 2);
    EXPECT_EQ(registry.entries().first().root, b);
    EXPECT_EQ(registry.entries().first().name, QStringLiteral("beta"));

    // Re-remembering moves an existing vault to the front instead of duplicating.
    registry.remember(a);
    EXPECT_EQ(registry.count(), 2);
    EXPECT_EQ(registry.entries().first().root, a);
    EXPECT_EQ(registry.mostRecent(), a);

    VaultRegistry reloaded;
    reloaded.load();
    ASSERT_EQ(reloaded.count(), 2);
    EXPECT_EQ(reloaded.entries().first().root, a);

    EXPECT_TRUE(reloaded.forget(a));
    EXPECT_FALSE(reloaded.forget(a));
    EXPECT_EQ(reloaded.count(), 1);
}

TEST(VaultRegistryTest, MostRecentSkipsVaultsThatNoLongerExist) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());
    const QString real = dir.path() + QStringLiteral("/real");
    QDir().mkpath(real);

    VaultRegistry registry;
    registry.remember(real);
    registry.remember(dir.path() + QStringLiteral("/deleted"));
    EXPECT_EQ(registry.mostRecent(), real);
}

TEST(VaultRegistryTest, DeserializeToleratesJunkAndDuplicates) {
    EXPECT_TRUE(VaultRegistry::deserialize(QByteArray()).isEmpty());
    EXPECT_TRUE(VaultRegistry::deserialize(QByteArray("not json at all")).isEmpty());
    // A bare array, hand-edited, still loads; the duplicate collapses.
    const QByteArray bare = R"([{"root":"/tmp/v"},{"root":"/tmp/v"}])";
    const auto entries = VaultRegistry::deserialize(bare);
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries.first().name, QStringLiteral("v"));
}

// A vault must never be a way to read or write outside itself, so a link target
// that tries to climb out is refused rather than followed.
TEST(Vault, TraversingWikiTargetsCannotEscapeTheRoot) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);
    // A real file just outside the vault, which a traversal would otherwise find.
    writeNote(dir.path() + QStringLiteral("/secret.md"), QStringLiteral("# secret\n"));

    Vault vault;
    vault.setRoot(root);
    const QString index = root + QStringLiteral("/index.md");

    for (const QString& target : {QStringLiteral("../secret"),
                                  QStringLiteral("../secret.md"),
                                  QStringLiteral("daily/../../secret"),
                                  dir.path() + QStringLiteral("/secret.md")}) {
        EXPECT_TRUE(vault.resolve(target, index, true).isEmpty()) << target.toStdString();
        EXPECT_TRUE(vault.newNotePath(target, index).isEmpty()) << target.toStdString();
    }
    // The legitimate in-vault path still works.
    EXPECT_EQ(vault.resolve(QStringLiteral("daily/monday"), index, true),
              root + QStringLiteral("/daily/monday.md"));
}

TEST(Vault, CreateRefusesNamesThatAreReallyPaths) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = makeVaultTree(dir);

    Vault vault;
    vault.setRoot(root);
    QString err;
    for (const QString& name : {QStringLiteral("../escaped"),
                                QStringLiteral("sub/nested"),
                                QStringLiteral("/etc/passwd"),
                                QStringLiteral("~/inhome"),
                                QStringLiteral("..")}) {
        EXPECT_TRUE(vault.createNote(root, name, &err).isEmpty()) << name.toStdString();
        EXPECT_FALSE(err.isEmpty());
        EXPECT_TRUE(vault.createFolder(root, name, &err).isEmpty()) << name.toStdString();
    }
    EXPECT_FALSE(QFile::exists(dir.path() + QStringLiteral("/escaped.md")));
    // A plain name is still fine.
    EXPECT_FALSE(vault.createNote(root, QStringLiteral("plain name"), &err).isEmpty());
}

TEST(VaultPure, IsSafeLeafNameRejectsSeparatorsAndDotDot) {
    EXPECT_TRUE(Vault::isSafeLeafName(QStringLiteral("a note")));
    EXPECT_TRUE(Vault::isSafeLeafName(QStringLiteral("note.md")));
    EXPECT_FALSE(Vault::isSafeLeafName(QString()));
    EXPECT_FALSE(Vault::isSafeLeafName(QStringLiteral("  ")));
    EXPECT_FALSE(Vault::isSafeLeafName(QStringLiteral(".")));
    EXPECT_FALSE(Vault::isSafeLeafName(QStringLiteral("..")));
    EXPECT_FALSE(Vault::isSafeLeafName(QStringLiteral("a/b")));
    EXPECT_FALSE(Vault::isSafeLeafName(QStringLiteral("a\\b")));
    EXPECT_FALSE(Vault::isSafeLeafName(QStringLiteral("~/a")));
}

// A new note is markdown unless the user explicitly asks for another note kind.
TEST(VaultCreate, DefaultsToMarkdownAndHonoursAnExplicitSuffix) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path();
    Vault vault;
    vault.setRoot(root);

    QString err;
    // No suffix -> .md
    EXPECT_EQ(QFileInfo(vault.createNote(root, QStringLiteral("plain"), &err)).fileName(),
              QStringLiteral("plain.md"));
    // Explicit .txt is respected rather than becoming "notes.txt.md".
    EXPECT_EQ(QFileInfo(vault.createNote(root, QStringLiteral("scratch.txt"), &err)).fileName(),
              QStringLiteral("scratch.txt"));
    EXPECT_EQ(QFileInfo(vault.createNote(root, QStringLiteral("read.markdown"), &err)).fileName(),
              QStringLiteral("read.markdown"));
    // A non-note suffix is a name, not a kind: "budget.2024" stays a markdown
    // note so a stray dot cannot produce a file the tree then refuses to show.
    EXPECT_EQ(QFileInfo(vault.createNote(root, QStringLiteral("budget.2024"), &err)).fileName(),
              QStringLiteral("budget.2024.md"));
}

// Suffix matching is case-insensitive, so "Notes.MD" is not doubled up.
TEST(VaultCreate, SuffixDetectionIgnoresCase) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path();
    Vault vault;
    vault.setRoot(root);

    QString err;
    const QString path = vault.createNote(root, QStringLiteral("Loud.MD"), &err);
    EXPECT_EQ(QFileInfo(path).suffix().toLower(), QStringLiteral("md"));
    EXPECT_FALSE(QFileInfo(path).fileName().endsWith(QStringLiteral(".md.md")));
}

// A root with a trailing slash is the same vault. contains() works by prefix
// plus a '/' boundary, so an unnormalised "/notes/" would report every file in
// the vault as outside it and silently switch wikilinks and backlinks off.
TEST(VaultRoot, TrailingSlashRootIsNormalised) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/notes");
    ASSERT_TRUE(QDir().mkpath(root));
    writeNote(root + QStringLiteral("/index.md"), QStringLiteral("# index\n\nto [[deep]]\n"));
    writeNote(root + QStringLiteral("/sub/deep.md"), QStringLiteral("# deep\n"));

    Vault vault;
    vault.setRoot(root + QStringLiteral("/"));
    vault.refresh();

    // The stored root has no trailing slash...
    EXPECT_EQ(vault.root(), root);
    // ...so files inside are recognised as inside.
    EXPECT_TRUE(vault.contains(root + QStringLiteral("/index.md")));
    EXPECT_TRUE(vault.contains(root + QStringLiteral("/sub/deep.md")));
    // And wikilinks still resolve, which is what silently broke.
    EXPECT_EQ(vault.resolve(QStringLiteral("deep"), root + QStringLiteral("/index.md"), true),
              root + QStringLiteral("/sub/deep.md"));
    // The near-miss guard still holds.
    EXPECT_FALSE(vault.contains(root + QStringLiteral("-backup/other.md")));
}

// Redundant separators and "." segments are normalised the same way.
TEST(VaultRoot, MessyButValidRootStillResolves) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = dir.path() + QStringLiteral("/notes");
    ASSERT_TRUE(QDir().mkpath(root));
    writeNote(root + QStringLiteral("/one.md"), QStringLiteral("# one\n"));

    Vault vault;
    vault.setRoot(dir.path() + QStringLiteral("/./notes//"));
    vault.refresh();

    EXPECT_EQ(vault.root(), root);
    EXPECT_TRUE(vault.contains(root + QStringLiteral("/one.md")));
    EXPECT_FALSE(vault.resolve(QStringLiteral("one"), QString(), true).isEmpty());
}
