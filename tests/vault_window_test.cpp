#include "core/Buffer.h"
#include "core/BufferManager.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Vault.h"
#include "theme/ThemeManager.h"
#include "ui/CheatSheet.h"
#include "ui/MainWindow.h"
#include "ui/VaultSidebar.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QKeySequence>
#include <QShortcut>
#include <QSplitter>
#include <QThread>
#include <QTreeView>
#include <QTemporaryDir>
#include <QWidget>
#include <gtest/gtest.h>

#include <memory>

namespace {

void write(const QString& path, const QString& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(body.toUtf8());
}

QString makeVault(const QTemporaryDir& dir) {
    const QString root = dir.path() + QStringLiteral("/vault");
    write(root + QStringLiteral("/index.md"), QStringLiteral("# index\n\nto [[monday]]\n"));
    write(root + QStringLiteral("/daily/monday.md"), QStringLiteral("# monday\n"));
    return root;
}

// Owns a live window plus the objects it borrows, and tears them down in the
// order the real app does: window first, then the buffers whose documents its
// editors were bound to.
struct Harness {
    std::unique_ptr<BufferManager> buffers;
    std::unique_ptr<ThemeManager> themes;
    std::unique_ptr<MainWindow> window;

    QWidget* sidebar() const {
        return window->findChild<QWidget*>(QStringLiteral("vaultSidebar"));
    }
};

Harness open(const Settings& settings) {
    Harness h;
    h.buffers = std::make_unique<BufferManager>();
    h.themes = std::make_unique<ThemeManager>();
    h.window = std::make_unique<MainWindow>(h.buffers.get(), h.themes.get(), settings);
    return h;
}

// Points loom's config and state at a scratch directory so a test never reads or
// writes the developer's real vault list.
void isolate(const QTemporaryDir& dir) {
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());
    qputenv("LOOM_CONFIG_DIR", (dir.path() + QStringLiteral("/config")).toUtf8());
}

Settings baseSettings() {
    Settings settings;
    settings.crtWipe = false;
    return settings;
}

} // namespace

// The central promise: with the feature off, the sidebar is not merely hidden,
// it was never built, and the vault list is not even read from disk.
TEST(VaultWindow, DisabledVaultBuildsNoSidebarAtAll) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);
    makeVault(dir);

    Settings settings = baseSettings();
    ASSERT_FALSE(settings.vaultEnabled);
    Harness h = open(settings);

    EXPECT_EQ(h.sidebar(), nullptr);
    EXPECT_FALSE(QFile::exists(Paths::vaultsFile()));
}

// A root without the flag, and a flag without a root, both stay off. There is no
// half-enabled state where the feature is on but points nowhere.
TEST(VaultWindow, RootWithoutTheFlagStaysOff) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultRoot = makeVault(dir);
    settings.vaultEnabled = false;
    settings.vaultSidebarVisible = true;
    EXPECT_TRUE(settings.activeVaultRoot().isEmpty());

    Harness h = open(settings);
    EXPECT_EQ(h.sidebar(), nullptr);

    Settings flagOnly = baseSettings();
    flagOnly.vaultEnabled = true;
    flagOnly.vaultRoot.clear();
    EXPECT_TRUE(flagOnly.activeVaultRoot().isEmpty());
}

TEST(VaultWindow, EnabledVaultShowsTheTree) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    settings.vaultSidebarVisible = true;

    Harness h = open(settings);
    QWidget* sidebar = h.sidebar();
    ASSERT_NE(sidebar, nullptr);
    EXPECT_TRUE(sidebar->isVisibleTo(h.window.get()));
    // Opening a vault records it, so the switcher has something to offer.
    EXPECT_TRUE(QFile::exists(Paths::vaultsFile()));
}

// "Gone" has to mean gone: no collapsed strip, and no splitter handle either.
TEST(VaultWindow, HidingTheTreeLeavesNoHandleBehind) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    settings.vaultSidebarVisible = true;

    Harness h = open(settings);
    QWidget* sidebar = h.sidebar();
    ASSERT_NE(sidebar, nullptr);
    auto* shell = qobject_cast<QSplitter*>(sidebar->parentWidget());
    ASSERT_NE(shell, nullptr);

    sidebar->hide();
    EXPECT_FALSE(sidebar->isVisibleTo(h.window.get()));
    for (int i = 0; i < shell->count(); ++i) {
        if (shell->widget(i) == sidebar) {
            ASSERT_NE(shell->handle(i), nullptr);
            EXPECT_FALSE(shell->handle(i)->isVisibleTo(shell));
        }
    }
}

// Hidden by default even with a vault configured: the tree is the optional part
// of the optional feature.
TEST(VaultWindow, SidebarStaysHiddenWhenTheSettingIsOff) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    settings.vaultSidebarVisible = false;

    Harness h = open(settings);
    QWidget* sidebar = h.sidebar();
    // Either never built, or built and hidden; both satisfy "no visual trace".
    EXPECT_TRUE(sidebar == nullptr || !sidebar->isVisibleTo(h.window.get()));
}

TEST(VaultWindow, ZenModeTakesTheSidebarWithIt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    settings.vaultSidebarVisible = true;
    settings.zenByDefault = true;

    Harness h = open(settings);
    if (QWidget* sidebar = h.sidebar()) {
        EXPECT_FALSE(sidebar->isVisibleTo(h.window.get()));
    }
}

// Enabling a vault must never touch the folder it points at: no dotfolder, no
// index, no metadata of any kind.
TEST(VaultWindow, OpeningAVaultWritesNothingIntoIt) {    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);
    const QString root = makeVault(dir);

    auto listing = [&root]() {
        QStringList out;
        QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            out << it.next();
        }
        out.sort();
        return out;
    };
    const QStringList before = listing();
    ASSERT_FALSE(before.isEmpty());

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = root;
    settings.vaultSidebarVisible = true;
    Harness h = open(settings);
    ASSERT_NE(h.window.get(), nullptr);

    EXPECT_EQ(listing(), before);
}

// The cheat sheet is hand-maintained, so it can drift from what wireShortcuts()
// actually binds. This ties the two together: every key the vault section
// advertises must exist as a real QShortcut on the window.
TEST(VaultWindow, EveryAdvertisedVaultShortcutIsActuallyBound) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    Harness h = open(settings);

    QList<QKeySequence> bound;
    for (const QShortcut* shortcut : h.window->findChildren<QShortcut*>()) {
        bound << shortcut->key();
    }

    int checked = 0;
    for (const ShortcutEntry& entry :
         ShortcutCatalog::search(QString(), QStringLiteral("vault"))) {
        const QKeySequence seq(entry.keys);
        // Skip rows that document syntax or keys handled somewhere other than a
        // window-level QShortcut: "[[note]]", "drag in tree", "Alt+N / Alt+D"
        // (VaultSidebar::keyPressEvent) and Alt+Left (Editor::keyPressEvent).
        if (seq.isEmpty() || entry.keys.contains(QLatin1Char('['))
            || entry.keys.contains(QLatin1Char('/')) || entry.keys.contains(QLatin1Char(' '))
            || entry.keys == QLatin1String("Alt+Left")) {
            continue;
        }
        ++checked;
        EXPECT_TRUE(bound.contains(seq))
            << entry.keys.toStdString() << " is in the cheat sheet but not bound";
    }
    // Guard against the filter above silently skipping everything.
    EXPECT_GE(checked, 4);

    // The converse: the four vault chords the window binds must all be
    // advertised, so a new shortcut cannot ship undiscoverable.
    QStringList advertised;
    for (const ShortcutEntry& entry :
         ShortcutCatalog::search(QString(), QStringLiteral("vault"))) {
        advertised << entry.keys;
    }
    for (const QString& key : {QStringLiteral("Ctrl+E"), QStringLiteral("Ctrl+Shift+E"),
                               QStringLiteral("Ctrl+Shift+B"), QStringLiteral("Ctrl+Shift+V")}) {
        EXPECT_TRUE(bound.contains(QKeySequence(key))) << key.toStdString() << " not bound";
        EXPECT_TRUE(advertised.contains(key)) << key.toStdString() << " not advertised";
    }
}

namespace {

// The tree the sidebar drives. Reached through the widget hierarchy so the tests
// exercise what the window actually built.
QTreeView* treeOf(const Harness& h) {
    QWidget* side = h.sidebar();
    return side ? side->findChild<QTreeView*>() : nullptr;
}

// Waits for QFileSystemModel, which lists directories on a worker thread.
bool waitForRows(QTreeView* tree, const QModelIndex& parent, int want) {
    for (int i = 0; i < 200; ++i) {
        if (tree->model() && tree->model()->rowCount(parent) >= want) {
            return true;
        }
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return false;
}

// The visible rows directly under an index, skipping hidden siblings.
QStringList visibleChildren(QTreeView* tree, const QModelIndex& parent) {
    QStringList names;
    if (!tree->model()) {
        return names;
    }
    for (int row = 0; row < tree->model()->rowCount(parent); ++row) {
        if (tree->isRowHidden(row, parent)) {
            continue;
        }
        names << tree->model()->index(row, 0, parent).data(Qt::DisplayRole).toString();
    }
    return names;
}

} // namespace

// The vault folder is the single top-level row: everything the user creates
// visibly hangs off one named parent.
TEST(VaultTree, VaultFolderIsTheOnlyTopLevelRow) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    // The vault gets a dedicated parent so the sibling set is exactly what this
    // test puts there. Those siblings must not leak into the view, since the
    // model is rooted at the parent directory to give the vault a top-level row.
    const QString space = dir.path() + QStringLiteral("/space");
    const QString root = space + QStringLiteral("/vault");
    write(root + QStringLiteral("/index.md"), QStringLiteral("# index\n"));
    QDir().mkpath(space + QStringLiteral("/not-the-vault"));
    write(space + QStringLiteral("/loose.md"), QStringLiteral("# loose\n"));

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = root;
    settings.vaultSidebarVisible = true;
    Harness h = open(settings);

    QTreeView* tree = treeOf(h);
    ASSERT_NE(tree, nullptr);
    // Wait for the whole parent listing, not just the first row: asserting after
    // only the vault has arrived would pass even with hiding disabled.
    ASSERT_TRUE(waitForRows(tree, tree->rootIndex(), 3))
        << "parent directory never finished listing";

    const QStringList top = visibleChildren(tree, tree->rootIndex());
    ASSERT_EQ(top.size(), 1) << top.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(top.first(), QStringLiteral("vault"));
    // And the vault row is expanded, so its notes are reachable without a click.
    EXPECT_TRUE(tree->isExpanded(tree->model()->index(0, 0, tree->rootIndex())));
}

// Notes show as "monday", not "monday.md", while folders keep their name.
TEST(VaultTree, ExtensionsAreHiddenButFoldersAreNot) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    settings.vaultSidebarVisible = true;
    Harness h = open(settings);

    QTreeView* tree = treeOf(h);
    ASSERT_NE(tree, nullptr);
    ASSERT_TRUE(waitForRows(tree, tree->rootIndex(), 1));
    const QModelIndex vaultRow = tree->model()->index(0, 0, tree->rootIndex());
    ASSERT_TRUE(waitForRows(tree, vaultRow, 2));

    const QStringList children = visibleChildren(tree, vaultRow);
    EXPECT_TRUE(children.contains(QStringLiteral("index")))
        << children.join(QStringLiteral(", ")).toStdString();
    // The folder keeps its name; no row anywhere shows a note extension.
    EXPECT_TRUE(children.contains(QStringLiteral("daily")));
    for (const QString& name : children) {
        EXPECT_FALSE(name.endsWith(QStringLiteral(".md"))) << name.toStdString();
    }
}

// Hiding the extension is presentation only: the model must still report the
// real filename for editing, or an in-place rename would silently drop ".md".
TEST(VaultTree, EditRoleStillCarriesTheRealFilename) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = makeVault(dir);
    settings.vaultSidebarVisible = true;
    Harness h = open(settings);

    QTreeView* tree = treeOf(h);
    ASSERT_NE(tree, nullptr);
    ASSERT_TRUE(waitForRows(tree, tree->rootIndex(), 1));
    const QModelIndex vaultRow = tree->model()->index(0, 0, tree->rootIndex());
    ASSERT_TRUE(waitForRows(tree, vaultRow, 2));

    bool checked = false;
    for (int row = 0; row < tree->model()->rowCount(vaultRow); ++row) {
        const QModelIndex index = tree->model()->index(row, 0, vaultRow);
        if (index.data(Qt::DisplayRole).toString() != QStringLiteral("index")) {
            continue;
        }
        EXPECT_EQ(index.data(Qt::EditRole).toString(), QStringLiteral("index.md"));
        checked = true;
    }
    EXPECT_TRUE(checked) << "never found the index row";
}

// Renaming through the tree must not lose the extension the tree is hiding.
// This is the rule itself, not a QFile::rename round-trip: the dialog shows
// "monday", so typing "tuesday" has to produce "tuesday.md" and never a
// suffixless "tuesday" that the vault would stop treating as a note.
TEST(VaultTree, RenameRestoresTheHiddenExtension) {
    // The common case: hidden ".md" comes back.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("monday.md"),
                                         QStringLiteral("tuesday"), false),
              QStringLiteral("tuesday.md"));
    // A deliberate change of kind is honoured rather than double-suffixed.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("monday.md"),
                                         QStringLiteral("tuesday.txt"), false),
              QStringLiteral("tuesday.txt"));
    // The original suffix is preserved, not normalised to ".md".
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("notes.txt"),
                                         QStringLiteral("scratch"), false),
              QStringLiteral("scratch.txt"));
    // An unknown suffix was never hidden, so it is left exactly as typed.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("data.2024"),
                                         QStringLiteral("budget"), false),
              QStringLiteral("budget"));
    // A dot in the new name is not a suffix change: "budget.2024" would become a
    // file the tree's filter hides, so the real extension is still appended.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("monday.md"),
                                         QStringLiteral("budget.2024"), false),
              QStringLiteral("budget.2024.md"));
    // Folder names are shown in full, so there is nothing to restore.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("daily"),
                                         QStringLiteral("weekly"), true),
              QStringLiteral("weekly"));
    // A folder that happens to look like a note keeps its literal name.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("notes.md"),
                                         QStringLiteral("archive"), true),
              QStringLiteral("archive"));
    // Whitespace is trimmed, and an empty answer cancels.
    EXPECT_EQ(VaultSidebar::renameTarget(QStringLiteral("monday.md"),
                                         QStringLiteral("  tuesday  "), false),
              QStringLiteral("tuesday.md"));
    EXPECT_TRUE(VaultSidebar::renameTarget(QStringLiteral("monday.md"),
                                           QStringLiteral("   "), false)
                    .isEmpty());
}

// Renamed notes stay indexed, which is what makes wikilinks keep working.
TEST(VaultTree, RenamedNoteIsStillFoundByTheIndex) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    const QString root = makeVault(dir);
    const QString from = root + QStringLiteral("/index.md");
    const QString to = root
                       + QStringLiteral("/")
                       + VaultSidebar::renameTarget(QStringLiteral("index.md"),
                                                    QStringLiteral("start"), false);
    ASSERT_TRUE(QFile::exists(from));
    ASSERT_TRUE(QFile::rename(from, to));
    EXPECT_TRUE(to.endsWith(QStringLiteral("/start.md")));

    Vault vault;
    vault.setRoot(root);
    vault.refresh();
    EXPECT_FALSE(vault.resolve(QStringLiteral("start"), QString(), true).isEmpty());
}

// A root written with a trailing slash in config.toml is the same vault. The
// sidebar compares paths against QFileSystemModel output, so an unnormalised
// root would make the "this is the vault folder" guard answer no — and offer to
// delete the whole vault from inside the tree showing it.
TEST(VaultTree, TrailingSlashRootStillIdentifiesTheVaultFolder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    isolate(dir);

    const QString root = makeVault(dir);
    Settings settings = baseSettings();
    settings.vaultEnabled = true;
    settings.vaultRoot = root + QStringLiteral("/");
    settings.vaultSidebarVisible = true;
    Harness h = open(settings);

    QTreeView* tree = treeOf(h);
    ASSERT_NE(tree, nullptr);
    ASSERT_TRUE(waitForRows(tree, tree->rootIndex(), 1));

    const QStringList top = visibleChildren(tree, tree->rootIndex());
    ASSERT_EQ(top.size(), 1) << top.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(top.first(), QStringLiteral("vault"));

    auto* side = qobject_cast<VaultSidebar*>(h.sidebar());
    ASSERT_NE(side, nullptr);
    // The stored root is normalised, with no trailing slash.
    EXPECT_EQ(side->rootPath(), root);
    // And so the vault folder is correctly refused as a rename/delete target,
    // while a note inside it is allowed.
    EXPECT_FALSE(side->isModifiable(root));
    EXPECT_FALSE(side->isModifiable(root + QStringLiteral("/")));
    EXPECT_TRUE(side->isModifiable(root + QStringLiteral("/index.md")));
}
