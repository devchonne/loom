#include "ui/VaultSidebar.h"

#include "core/Vault.h"
#include "theme/ChromeStyle.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

// Shows "note" for "note.md". Only the visible string changes: EditRole is left
// to the source model so renaming and every path lookup still see the real
// filename, and nothing about the file on disk is affected.
QVariant VaultTreeProxy::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole || index.column() != 0) {
        return QIdentityProxyModel::data(index, role);
    }
    const QVariant value = QIdentityProxyModel::data(index, role);
    const QString name = value.toString();
    if (name.isEmpty()) {
        return value;
    }
    // Folders keep their name verbatim: a folder called "notes.md" is still
    // "notes.md", and stripping there would be a lie about the filesystem.
    if (const auto* source = qobject_cast<const QFileSystemModel*>(sourceModel())) {
        if (source->isDir(mapToSource(index))) {
            return value;
        }
    }
    const QFileInfo info(name);
    // Only known note suffixes are hidden. Anything else is shown in full so an
    // unexpected file never masquerades as a note.
    if (!Vault::noteSuffixes().contains(info.suffix().toLower())) {
        return value;
    }
    const QString base = info.completeBaseName();
    // A dotfile like ".md" has no base to show, so leave it alone.
    return base.isEmpty() ? value : base;
}

VaultSidebar::VaultSidebar(Vault* vault, QWidget* parent)
    : QWidget(parent)
    , vault_(vault)
    , tree_(new QTreeView(this)) {
    setObjectName(QStringLiteral("vaultSidebar"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    tree_->setIndentation(12);
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);
    tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setFrameShape(QFrame::NoFrame);
    // Drag a note or a folder onto another folder to move it. Internal only:
    // the vault is never a drop target for outside files, so nothing arrives
    // in it by accident.
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDragEnabled(true);
    tree_->setAcceptDrops(true);
    tree_->setDropIndicatorShown(true);
    tree_->setDefaultDropAction(Qt::MoveAction);
    // Renames and drops are performed by QFileSystemModel itself, so the only
    // way to keep open tabs pointing at the right file is to watch what the
    // model actually did on disk. Without this, dragging a note would leave its
    // tab on a vanished path and the next autosave would recreate the file at
    // the old location.
    tree_->viewport()->installEventFilter(this);
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        const QString path = pathForViewIndex(index);
        if (path.isEmpty()) {
            return;
        }
        if (QFileInfo(path).isDir()) {
            tree_->setExpanded(index, !tree_->isExpanded(index));
            return;
        }
        emit openRequested(path);
    });
    connect(tree_, &QTreeView::customContextMenuRequested, this, &VaultSidebar::showContextMenu);

    restyle();
    hide();
}

void VaultSidebar::setTheme(const Theme& theme) {
    theme_ = theme;
    restyle();
}

void VaultSidebar::restyle() {
    // The tree is chrome, not a data view: no alternating rows, no frame, and
    // the same background as the surrounding window so it reads as part of it.
    setStyleSheet(QStringLiteral(R"(
QWidget#vaultSidebar {
    background: %1;
    border-right: 1px solid %2;
}
QWidget#vaultSidebar QTreeView {
    background: %1;
    color: %3;
    border: none;
    outline: none;
    alternate-background-color: %1;
    selection-background-color: %4;
    selection-color: %5;
    show-decoration-selected: 1;
}
QWidget#vaultSidebar QTreeView::item {
    padding: 2px 4px;
    border: none;
}
QWidget#vaultSidebar QTreeView::item:hover {
    background: %6;
}
QWidget#vaultSidebar QTreeView::item:selected {
    background: %4;
    color: %5;
}
)")
                      .arg(theme_.background.name(),
                           theme_.muted.name(),
                           theme_.foreground.name(),
                           theme_.selection.name(),
                           theme_.brightForeground.name(),
                           theme_.lighterBackground.name()));
}

void VaultSidebar::setChromeFont(const QFont& font) {
    setFont(font);
    tree_->setFont(font);
}

void VaultSidebar::setRoot(const QString& root) {
    // Matches Vault::setRoot exactly, so every comparison against a path from
    // QFileSystemModel (which reports absolute, cleaned paths) lines up. Without
    // cleanPath a trailing slash survives absoluteFilePath, and the "is this the
    // vault folder itself?" guard would answer no — offering to delete the whole
    // vault from inside the tree showing it.
    const QString trimmed = root.trimmed();
    const QString cleaned = trimmed.isEmpty()
                                ? QString()
                                : QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
    if (root_ == cleaned) {
        return;
    }
    root_ = cleaned;
    rebuildModel();
}

void VaultSidebar::rebuildModel() {
    if (root_.isEmpty() || !QFileInfo(root_).isDir()) {
        tree_->setModel(nullptr);
        delete proxy_;
        proxy_ = nullptr;
        delete model_;
        model_ = nullptr;
        return;
    }

    if (!model_) {
        model_ = new QFileSystemModel(this);
        model_->setReadOnly(false); // rename in place, and accept internal moves
        model_->setIconProvider(chromeIconProvider());
        // In-place renames (the edit key) are done by the model, so mirror them
        // onto open tabs. Drops are handled via the drag bookkeeping below.
        connect(model_, &QFileSystemModel::fileRenamed, this,
                [this](const QString& path, const QString& oldName, const QString& newName) {
                    const QString from = path + QLatin1Char('/') + oldName;
                    const QString to = path + QLatin1Char('/') + newName;
                    emit pathMoved(from, to);
                    if (vault_) {
                        vault_->refresh();
                    }
                });
        // A drop moves rows rather than renaming them, so the new path only
        // shows up as an insertion. Pair it with the path the drag started on.
        connect(model_, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex& parent, int first, int last) {
                    // Siblings of the vault appear asynchronously as the parent
                    // directory is listed, so re-hide on every insertion.
                    hideEverythingButTheVault();
                    if (dragging_.isEmpty() || !model_) {
                        return;
                    }
                    const QString name = QFileInfo(dragging_).fileName();
                    for (int row = first; row <= last; ++row) {
                        const QModelIndex index = model_->index(row, 0, parent);
                        if (!index.isValid()) {
                            continue;
                        }
                        const QFileInfo info = model_->fileInfo(index);
                        if (info.fileName() != name) {
                            continue;
                        }
                        const QString to = info.absoluteFilePath();
                        if (to == dragging_) {
                            continue;
                        }
                        const QString from = dragging_;
                        dragging_.clear();
                        emit pathMoved(from, to);
                        if (vault_) {
                            vault_->refresh();
                        }
                        return;
                    }
                });
        QStringList filters;
        for (const QString& suffix : Vault::noteSuffixes()) {
            filters << QStringLiteral("*.") + suffix;
        }
        model_->setNameFilters(filters);
        // Non-matching files stay hidden rather than greyed out: a vault view
        // should look like notes, not like a file manager.
        model_->setNameFilterDisables(false);
        model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

        proxy_ = new VaultTreeProxy(this);
        proxy_->setSourceModel(model_);
        tree_->setModel(proxy_);
        for (int column = 1; column < proxy_->columnCount(); ++column) {
            tree_->setColumnHidden(column, true);
        }
    }

    const QModelIndex vaultIndex = model_->setRootPath(root_);
    // The vault folder is the one top-level row, so the tree reads as a single
    // named container. That means rooting the view at the vault's *parent* and
    // hiding everything there except the vault itself.
    const QModelIndex parentIndex = vaultIndex.parent();
    if (parentIndex.isValid()) {
        tree_->setRootIndex(proxy_->mapFromSource(parentIndex));
        hideEverythingButTheVault();
        tree_->setExpanded(proxy_->mapFromSource(vaultIndex), true);
    } else {
        // A vault at a filesystem root has no parent to hang it from.
        tree_->setRootIndex(proxy_->mapFromSource(vaultIndex));
    }
}

// Keeps the vault's siblings out of sight. The view is rooted at the parent
// directory to give the vault a visible top-level row, which would otherwise
// expose every neighbouring folder.
void VaultSidebar::hideEverythingButTheVault() {
    if (!model_ || !proxy_ || root_.isEmpty()) {
        return;
    }
    const QModelIndex vaultIndex = proxy_->mapFromSource(model_->index(root_));
    const QModelIndex parentIndex = vaultIndex.parent();
    if (!vaultIndex.isValid() || !parentIndex.isValid()
        || parentIndex != tree_->rootIndex()) {
        return;
    }
    const int rows = proxy_->rowCount(parentIndex);
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = proxy_->index(row, 0, parentIndex);
        tree_->setRowHidden(row, parentIndex, index != vaultIndex);
    }
}

QModelIndex VaultSidebar::indexForPath(const QString& path) const {
    if (!model_ || path.isEmpty()) {
        return {};
    }
    return model_->index(path);
}

QModelIndex VaultSidebar::viewIndexForPath(const QString& path) const {
    const QModelIndex index = indexForPath(path);
    if (!proxy_ || !index.isValid()) {
        return {};
    }
    return proxy_->mapFromSource(index);
}

QString VaultSidebar::pathForViewIndex(const QModelIndex& index) const {
    if (!model_ || !proxy_ || !index.isValid()) {
        return {};
    }
    return model_->fileInfo(proxy_->mapToSource(index)).absoluteFilePath();
}

void VaultSidebar::syncCurrentPath(const QString& path) {
    if (!model_ || path.isEmpty() || !vault_ || !vault_->contains(path)) {
        return;
    }
    const QModelIndex index = viewIndexForPath(path);
    if (!index.isValid()) {
        return;
    }
    const QSignalBlocker block(tree_);
    tree_->setCurrentIndex(index);
    tree_->scrollTo(index, QAbstractItemView::EnsureVisible);
}

void VaultSidebar::revealPath(const QString& path) {
    if (!model_) {
        return;
    }
    const QModelIndex index = viewIndexForPath(path);
    if (!index.isValid()) {
        return;
    }
    // Expand every ancestor, not just the immediate parent: a note two folders
    // deep would otherwise be selected while still collapsed out of view.
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
        tree_->expand(parent);
    }
    tree_->setCurrentIndex(index);
    tree_->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void VaultSidebar::focusTree() {
    tree_->setFocus();
    if (proxy_ && !tree_->currentIndex().isValid()) {
        // The vault row itself, so arrow keys start somewhere sensible.
        const QModelIndex vaultIndex = viewIndexForPath(root_);
        tree_->setCurrentIndex(vaultIndex.isValid()
                                   ? vaultIndex
                                   : proxy_->index(0, 0, tree_->rootIndex()));
    }
}

QString VaultSidebar::selectedPath() const {
    if (!model_) {
        return {};
    }
    const QString path = pathForViewIndex(tree_->currentIndex());
    return path.isEmpty() ? root_ : path;
}

QString VaultSidebar::selectedDir() const {
    const QString path = selectedPath();
    if (path.isEmpty()) {
        return root_;
    }
    const QFileInfo info(path);
    return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
}

void VaultSidebar::newNoteInSelection() {
    if (!vault_ || root_.isEmpty()) {
        return;
    }
    const QString dir = selectedDir();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("new note"),
                                               QStringLiteral("name (.md unless you say otherwise)"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    QString err;
    const QString path = vault_->createNote(dir, name, &err);
    if (path.isEmpty()) {
        return;
    }
    vault_->refresh();
    revealPath(path);
    emit openRequested(path);
}

void VaultSidebar::newFolderInSelection() {
    if (!vault_ || root_.isEmpty()) {
        return;
    }
    const QString dir = selectedDir();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("new folder"),
                                               QStringLiteral("name"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    QString err;
    const QString path = vault_->createFolder(dir, name, &err);
    if (path.isEmpty()) {
        return;
    }
    vault_->refresh();
    revealPath(path);
}

// Refuses the vault folder itself, whatever spelling it arrives in. Paths from
// QFileSystemModel are already clean, but this also backs the public guard used
// before renaming and deleting, so it normalises rather than assuming.
bool VaultSidebar::isModifiable(const QString& path) const {
    if (path.isEmpty() || root_.isEmpty()) {
        return false;
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()) != root_;
}

// Restores the extension the tree hides, unless the user typed a note suffix of
// their own. Rejecting unknown suffixes matters: "budget.2024" must not become a
// file the name filter then refuses to show, so it keeps the original ".md" and
// becomes "budget.2024.md".
QString VaultSidebar::renameTarget(const QString& currentName, const QString& typed, bool isDir) {
    const QString trimmed = typed.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    // Folder names are shown in full, so there is nothing to restore.
    if (isDir) {
        return trimmed;
    }
    const QFileInfo current(currentName);
    const QString currentSuffix = current.suffix();
    // Nothing was hidden, so nothing is put back.
    if (!Vault::noteSuffixes().contains(currentSuffix.toLower())) {
        return trimmed;
    }
    if (Vault::noteSuffixes().contains(QFileInfo(trimmed).suffix().toLower())) {
        return trimmed;
    }
    return trimmed + QLatin1Char('.') + currentSuffix;
}

void VaultSidebar::renameSelection() {
    if (!model_ || root_.isEmpty()) {
        return;
    }
    const QString from = selectedPath();
    // The vault folder is the tree's container, not a row to rename from here.
    if (!isModifiable(from)) {
        return;
    }
    const QFileInfo info(from);
    // Extensions are hidden in the tree, so the dialog is seeded with what the
    // user actually sees. renameTarget() puts the suffix back.
    const bool hidden = !info.isDir()
                        && Vault::noteSuffixes().contains(info.suffix().toLower());
    const QString seed = hidden ? info.completeBaseName() : info.fileName();

    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("rename"), QStringLiteral("name"),
                                               QLineEdit::Normal, seed, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == seed) {
        return;
    }
    const QString target = renameTarget(info.fileName(), name, info.isDir());
    if (target.isEmpty() || !Vault::isSafeLeafName(target)) {
        return;
    }
    const QString to = info.absolutePath() + QLatin1Char('/') + target;
    if (to == from || QFileInfo::exists(to)) {
        return;
    }
    if (!QFile::rename(from, to)) {
        return;
    }
    // Open tabs follow the file rather than silently pointing at a path that no
    // longer exists.
    emit pathMoved(from, to);
    if (vault_) {
        vault_->refresh();
    }
    revealPath(to);
}

void VaultSidebar::deleteSelection() {
    if (!model_ || root_.isEmpty()) {
        return;
    }
    const QString path = selectedPath();
    // Never offer to delete the vault folder itself from the tree showing it.
    if (!isModifiable(path)) {
        return;
    }
    const QFileInfo info(path);
    // Deletion is destructive and there is no trash, so it always confirms.
    const QString question = info.isDir()
                                 ? QStringLiteral("delete folder \"%1\" and everything in it?")
                                       .arg(info.fileName())
                                 : QStringLiteral("delete \"%1\"?").arg(info.fileName());
    if (QMessageBox::question(this, QStringLiteral("delete"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    const bool ok = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!ok) {
        return;
    }
    emit pathDeleted(path);
    if (vault_) {
        vault_->refresh();
    }
}

void VaultSidebar::showContextMenu(const QPoint& pos) {
    if (!model_ || !proxy_) {
        return;
    }
    const QModelIndex index = tree_->indexAt(pos);
    if (index.isValid()) {
        tree_->setCurrentIndex(index);
    }
    const QString path = selectedPath();
    const bool isDir = !path.isEmpty() && QFileInfo(path).isDir();

    QMenu menu(this);
    menu.addAction(QStringLiteral("new note"), this, &VaultSidebar::newNoteInSelection);
    menu.addAction(QStringLiteral("new folder"), this, &VaultSidebar::newFolderInSelection);
    // The vault row can be revealed but not renamed or deleted from inside the
    // tree that is showing it.
    if (index.isValid() && isModifiable(path)) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("rename"), this, &VaultSidebar::renameSelection);
        menu.addAction(QStringLiteral("delete"), this, &VaultSidebar::deleteSelection);
    }
    menu.addSeparator();
    menu.addAction(isDir ? QStringLiteral("show folder in file manager")
                         : QStringLiteral("show file in file manager"),
                   this, &VaultSidebar::revealInFileManager);
    menu.addSeparator();
    menu.addAction(QStringLiteral("refresh"), this, [this]() {
        if (vault_) {
            vault_->refresh();
        }
        rebuildModel();
    });
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

// Opens the desktop file manager on the selection. Folders open directly; for a
// note the containing folder is opened with the file selected where the file
// manager supports it, so "show file" really does point at the file.
void VaultSidebar::revealInFileManager() {
    const QString path = selectedPath();
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        return;
    }
    if (info.isDir()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
        return;
    }
    // Most Linux file managers implement this interface, and it is the only
    // portable way to have the file itself highlighted rather than just its
    // folder opened. Falls back to opening the folder when the call fails.
    const bool shown = QProcess::startDetached(
        QStringLiteral("dbus-send"),
        {QStringLiteral("--session"), QStringLiteral("--print-reply"),
         QStringLiteral("--dest=org.freedesktop.FileManager1"),
         QStringLiteral("/org/freedesktop/FileManager1"),
         QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
         QStringLiteral("array:string:") + QUrl::fromLocalFile(info.absoluteFilePath()).toString(),
         QStringLiteral("string:")});
    if (!shown) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
}

// Records which path a drag started on, so the matching insertion can be turned
// into a (from, to) move. QFileSystemModel offers no "this row moved here"
// signal, only an insert plus a remove.
bool VaultSidebar::eventFilter(QObject* watched, QEvent* event) {
    if (model_ && watched == tree_->viewport() && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        dragging_ = pathForViewIndex(tree_->indexAt(mouse->pos()));
        // The vault folder is the tree's container; dragging it into itself is
        // not a meaningful move.
        if (dragging_ == root_) {
            dragging_.clear();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VaultSidebar::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        emit closeRequested();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QModelIndex index = tree_->currentIndex();
        const QString path = pathForViewIndex(index);
        if (!path.isEmpty()) {
            if (QFileInfo(path).isDir()) {
                tree_->setExpanded(index, !tree_->isExpanded(index));
            } else {
                emit openRequested(path);
            }
        }
        return;
    }
    case Qt::Key_Delete:
        deleteSelection();
        return;
    case Qt::Key_N:
        if (event->modifiers() & Qt::AltModifier) {
            newNoteInSelection();
            return;
        }
        break;
    case Qt::Key_D:
        if (event->modifiers() & Qt::AltModifier) {
            newFolderInSelection();
            return;
        }
        break;
    case Qt::Key_R:
        if (event->modifiers() & Qt::AltModifier) {
            renameSelection();
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}
