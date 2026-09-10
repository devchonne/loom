#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// One remembered vault. Nothing here is ever written inside the vault folder
// itself: a vault stays a plain directory of plain markdown files.
struct VaultEntry {
    QString root;   // absolute path
    QString name;   // display name, defaults to the folder name
    qint64 lastOpenedAt = 0;
};

// The list of vaults the user has opened, in most-recent-first order, persisted
// to ~/.local/state/loom/vaults.json the same way stations.json is.
//
// The registry is only ever consulted when the vault feature is on. With it off,
// the file is not even read.
class VaultRegistry : public QObject {
    Q_OBJECT

public:
    explicit VaultRegistry(QObject* parent = nullptr);

    void load();
    bool save(QString* error = nullptr) const;

    const QVector<VaultEntry>& entries() const { return entries_; }
    bool isEmpty() const { return entries_.isEmpty(); }
    int count() const { return int(entries_.size()); }

    // Adds the root if it is new, stamps lastOpenedAt, moves it to the front,
    // and persists. Returns the normalised absolute path.
    QString remember(const QString& root);
    bool forget(const QString& root);
    // Most recently opened root that still exists on disk.
    QString mostRecent() const;

    static QString serialize(const QVector<VaultEntry>& entries);
    static QVector<VaultEntry> deserialize(const QByteArray& bytes);
    static QString normalize(const QString& root);

signals:
    void changed();

private:
    int indexOf(const QString& root) const;

    QVector<VaultEntry> entries_;
};
