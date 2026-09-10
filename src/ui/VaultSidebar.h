#pragma once

#include "theme/Theme.h"

#include <QIdentityProxyModel>
#include <QString>
#include <QWidget>

// Hides note extensions in the tree.
//
// Display only: EditRole still carries the real filename, so an in-place rename
// starts from "note.md" rather than from a name that would lose its suffix. Kept
// in the header so the sidebar can own one by pointer.
class VaultTreeProxy : public QIdentityProxyModel {
    Q_OBJECT

public:
    using QIdentityProxyModel::QIdentityProxyModel;

    QVariant data(const QModelIndex& index, int role) const override;
};

class Vault;
class QTreeView;
class QFileSystemModel;
class QModelIndex;

// The vault tree: folders and notes, create / rename / delete / drag to move.
//
// The vault folder itself is the single top-level row, so everything visibly
// lives under one named parent. Names are shown without their extension; the
// suffix is still on disk and is put back on rename.
//
// Deliberately built lazily and destroyed-by-hiding: when the sidebar is off
// there is no widget, no model, and no visual trace of it anywhere in the
// chrome. Toggling it is not "collapse to a strip", it is gone.
class VaultSidebar : public QWidget {
    Q_OBJECT

public:
    explicit VaultSidebar(Vault* vault, QWidget* parent = nullptr);

    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    // Points the tree at the vault root. Empty tears the model down.
    void setRoot(const QString& root);
    // Highlights the file the editor currently shows, without stealing focus.
    void syncCurrentPath(const QString& path);
    void focusTree();

    // Actions, also reachable from slash commands and the cheat sheet.
    void newNoteInSelection();
    void newFolderInSelection();
    void renameSelection();
    void deleteSelection();
    void revealPath(const QString& path);
    // Name a rename should land on, given the file's current name and what the
    // user typed into a tree that hides extensions. Pure and static so the rule
    // is testable without driving a modal dialog.
    //
    // "monday.md" + "tuesday"     -> "tuesday.md"  (hidden suffix restored)
    // "monday.md" + "tuesday.txt" -> "tuesday.txt" (explicit change honoured)
    // "notes.2024" + "budget"     -> "budget"      (nothing was hidden)
    // Absolute, normalised vault root, or empty when the tree has none. Every
    // "is this the vault row itself?" guard compares against this, so it has to
    // match the form QFileSystemModel reports.
    QString rootPath() const { return root_; }

    // True when the path is a row the tree is allowed to rename or delete. The
    // vault folder itself is the container the tree is showing, not a row to
    // destroy from inside it.
    //
    // Normalises its argument rather than trusting the caller: this guards
    // destructive actions, so "/vault/" must not slip past a check for
    // "/vault".
    bool isModifiable(const QString& path) const;

    static QString renameTarget(const QString& currentName, const QString& typed, bool isDir);

    // Hands the selection to the desktop file manager: the folder itself, or
    // the folder containing the selected note.
    void revealInFileManager();

signals:
    // Single click or Enter on a note.
    void openRequested(const QString& path);
    // A file was renamed or moved on disk; open buffers need to follow it.
    void pathMoved(const QString& from, const QString& to);
    void pathDeleted(const QString& path);
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildModel();
    void restyle();
    // Hides the vault's siblings, which the parent-rooted view would show.
    void hideEverythingButTheVault();
    void showContextMenu(const QPoint& pos);
    // Absolute path of the selected row, or the root when nothing is selected.
    QString selectedPath() const;
    // Directory new items should go into: the selection if it is a folder, else
    // the folder that contains it.
    QString selectedDir() const;
    // Source-model index for a path. Use viewIndexForPath() to talk to the view.
    QModelIndex indexForPath(const QString& path) const;
    // Proxy index for a path, which is what the tree actually addresses.
    QModelIndex viewIndexForPath(const QString& path) const;
    // Absolute path behind a view (proxy) index.
    QString pathForViewIndex(const QModelIndex& index) const;

    Vault* vault_ = nullptr;
    QTreeView* tree_ = nullptr;
    QFileSystemModel* model_ = nullptr;
    // Strips the extension from what the tree shows, without touching what the
    // model reports for editing or what exists on disk.
    VaultTreeProxy* proxy_ = nullptr;
    Theme theme_ = Theme::builtin();
    QString root_;
    // Path a drag started on, used to pair a drop's insertion with its origin.
    QString dragging_;
};
