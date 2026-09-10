#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QFileSystemWatcher;
class QTimer;

// One note in the vault index.
struct VaultNote {
    QString path;     // absolute
    QString relative; // relative to the vault root, suffix included
    QString name;     // file name without suffix: what [[wikilinks]] address
};

// One reference to a note, found while scanning another note.
struct VaultBacklink {
    QString path;     // absolute path of the *referring* note
    QString relative; // its path relative to the root
    QString name;     // its name without suffix
    int line = 0;     // 1-based line number of the reference
    QString context;  // the referring line, trimmed
};

// A vault is one folder, and nothing more. It never writes metadata inside that
// folder, never moves anything, and never changes how a document is stored: it
// only answers "which file does this name mean" and "who points at this file".
//
// A Vault with an empty root is inert. That is the state loom is in unless the
// user has explicitly turned the feature on, and it is also why the whole
// feature can be a single null check at every call site rather than a mode that
// the rest of the app has to reason about.
class Vault : public QObject {
    Q_OBJECT

public:
    explicit Vault(QObject* parent = nullptr);

    // Empty root means "no vault". Anything already indexed is dropped.
    void setRoot(const QString& root);
    QString root() const { return root_; }
    // True when a root is set and still exists on disk.
    bool isOpen() const;
    // Display name: the root's own folder name.
    QString name() const;

    // Whether an absolute file path lives under the root. Buffers outside the
    // vault keep loom's ordinary, vault-free behaviour even while a vault is
    // open, which is what makes the feature a place rather than a mode.
    bool contains(const QString& path) const;
    QString relativePath(const QString& path) const;

    // Rescans the tree. Cheap: names and paths only, no file contents.
    void refresh();
    const QVector<VaultNote>& notes() const { return notes_; }
    int count() const { return int(notes_.size()); }

    // Resolves a link target to an absolute path, or returns empty.
    //
    // `wiki` targets are looked up by note name across the whole vault (with a
    // preference for the closest match to `fromPath`); plain targets are only
    // ever resolved as paths. Targets are resolved relative to `fromPath` when
    // there is no vault, or when `fromPath` sits outside it, so a document
    // written inside a vault keeps working after the vault is switched off.
    QString resolve(const QString& target, const QString& fromPath, bool wiki) const;

    // Where an unresolved wiki target should be created.
    QString newNotePath(const QString& target, const QString& fromPath) const;

    // Creates `<dir>/<name>.md` (de-duplicating the name) seeded with an H1.
    // Returns the absolute path, or empty on failure.
    QString createNote(const QString& dir, const QString& name, QString* error = nullptr) const;
    QString createFolder(const QString& dir, const QString& name, QString* error = nullptr) const;

    // Every note that links to `path`. Reads file contents, so this is called on
    // demand from the backlinks overlay rather than kept continuously warm.
    QVector<VaultBacklink> backlinks(const QString& path) const;

    // Pure helpers, static so the resolution rules are unit testable without
    // touching a disk or constructing a Vault.
    static QStringList noteSuffixes();
    static bool isNote(const QString& path);
    static QString stripSuffix(const QString& fileName);
    // Case- and whitespace-insensitive key a wiki target is matched on.
    static QString normalizeName(const QString& text);
    // Splits "Note#Heading" into its file part and its anchor.
    static void splitAnchor(const QString& target, QString* name, QString* anchor);
    // Resolves a target as a path relative to `fromDir`, trying the bare target
    // first and then each note suffix. Empty when nothing exists.
    static QString resolveRelative(const QString& target, const QString& fromDir);
    // Rejects anything that is not a plain file or folder name: no separators,
    // no "..", no absolute or "~" paths. Create operations take user text
    // straight from a dialog or a slash command, so they all check this.
    static bool isSafeLeafName(const QString& name);

signals:
    void rootChanged();
    void indexChanged();

private:
    void rewatch();
    void scheduleRescan();
    QString bestMatch(const QStringList& candidates, const QString& fromPath) const;

    QString root_;
    QVector<VaultNote> notes_;
    // normalized name -> absolute paths, sorted, for wiki target lookup.
    QHash<QString, QStringList> byName_;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer* rescan_ = nullptr;
};
