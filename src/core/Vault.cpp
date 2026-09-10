#include "core/Vault.h"

#include "core/AtomicFile.h"
#include "markdown/MarkdownRules.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace {

// inotify watches are a finite resource, and a vault can be arbitrarily deep.
// Past this many directories the tree simply stops being watched; the index is
// still correct, it just refreshes on demand instead of automatically.
constexpr int kMaxWatchedDirs = 512;
// Guard against someone pointing the vault at their home directory.
constexpr int kMaxIndexedNotes = 20000;

QString collapse(const QString& text) {
    return text.simplified();
}

} // namespace

Vault::Vault(QObject* parent)
    : QObject(parent)
    , watcher_(new QFileSystemWatcher(this))
    , rescan_(new QTimer(this)) {
    rescan_->setSingleShot(true);
    rescan_->setInterval(250);
    connect(rescan_, &QTimer::timeout, this, &Vault::refresh);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { scheduleRescan(); });
}

QStringList Vault::noteSuffixes() {
    return {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("txt")};
}

bool Vault::isNote(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return noteSuffixes().contains(suffix);
}

QString Vault::stripSuffix(const QString& fileName) {
    const QFileInfo info(fileName);
    return noteSuffixes().contains(info.suffix().toLower()) ? info.completeBaseName() : fileName;
}

QString Vault::normalizeName(const QString& text) {
    return collapse(text).toLower();
}

void Vault::splitAnchor(const QString& target, QString* name, QString* anchor) {
    // A leading '#' is a same-document anchor, not a file with an anchor, and is
    // handled by the editor before it ever reaches the vault.
    const int hash = target.indexOf(QLatin1Char('#'), 1);
    if (hash > 0) {
        if (name) {
            *name = target.left(hash).trimmed();
        }
        if (anchor) {
            *anchor = target.mid(hash + 1).trimmed();
        }
        return;
    }
    if (name) {
        *name = target.trimmed();
    }
    if (anchor) {
        anchor->clear();
    }
}

QString Vault::resolveRelative(const QString& target, const QString& fromDir) {
    QString spec = target.trimmed();
    if (spec.isEmpty()) {
        return {};
    }
    if (spec.startsWith(QLatin1Char('~'))) {
        spec = QDir::homePath() + spec.mid(1);
    }

    QStringList tries;
    if (QFileInfo(spec).isAbsolute()) {
        tries << spec;
    } else {
        if (fromDir.isEmpty()) {
            return {};
        }
        tries << QDir(fromDir).absoluteFilePath(spec);
    }
    // A wiki target, and often a plain one, is written without its suffix.
    const QString base = tries.first();
    if (QFileInfo(base).suffix().isEmpty()) {
        for (const QString& suffix : noteSuffixes()) {
            tries << base + QLatin1Char('.') + suffix;
        }
    }
    for (const QString& candidate : tries) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

bool Vault::isSafeLeafName(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String(".") || trimmed == QLatin1String("..")) {
        return false;
    }
    if (trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))) {
        return false;
    }
    return !trimmed.startsWith(QLatin1Char('~'));
}

void Vault::setRoot(const QString& root) {
    // cleanPath on top of absoluteFilePath matters: absoluteFilePath keeps a
    // trailing slash, and contains() compares by prefix plus a '/' boundary. A
    // root of "/notes/" would therefore report every file in the vault as
    // outside it, silently disabling wikilinks, backlinks and tree sync.
    const QString next = root.trimmed().isEmpty()
                             ? QString()
                             : QDir::cleanPath(QFileInfo(root.trimmed()).absoluteFilePath());
    if (next == root_) {
        return;
    }
    root_ = next;
    notes_.clear();
    byName_.clear();
    const QStringList watched = watcher_->directories() + watcher_->files();
    if (!watched.isEmpty()) {
        watcher_->removePaths(watched);
    }
    emit rootChanged();
    if (root_.isEmpty()) {
        emit indexChanged();
        return;
    }
    refresh();
}

bool Vault::isOpen() const {
    return !root_.isEmpty() && QFileInfo(root_).isDir();
}

QString Vault::name() const {
    return root_.isEmpty() ? QString() : QDir(root_).dirName();
}

bool Vault::contains(const QString& path) const {
    if (root_.isEmpty() || path.isEmpty()) {
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (!absolute.startsWith(root_)) {
        return false;
    }
    // Guard against `/home/me/vault-backup` matching root `/home/me/vault`.
    return absolute.size() == root_.size()
           || absolute.at(root_.size()) == QLatin1Char('/');
}

QString Vault::relativePath(const QString& path) const {
    if (!contains(path)) {
        return {};
    }
    return QDir(root_).relativeFilePath(QFileInfo(path).absoluteFilePath());
}

void Vault::scheduleRescan() {
    rescan_->start();
}

void Vault::rewatch() {
    const QStringList watched = watcher_->directories() + watcher_->files();
    if (!watched.isEmpty()) {
        watcher_->removePaths(watched);
    }
    if (!isOpen()) {
        return;
    }
    QStringList dirs{root_};
    QDirIterator it(root_, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && dirs.size() < kMaxWatchedDirs) {
        const QString dir = it.next();
        if (QFileInfo(dir).fileName().startsWith(QLatin1Char('.'))) {
            continue;
        }
        dirs << dir;
    }
    watcher_->addPaths(dirs);
}

void Vault::refresh() {
    notes_.clear();
    byName_.clear();
    if (!isOpen()) {
        rewatch();
        emit indexChanged();
        return;
    }

    QStringList filters;
    for (const QString& suffix : noteSuffixes()) {
        filters << QStringLiteral("*.") + suffix;
    }
    QDirIterator it(root_, filters, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir base(root_);
    while (it.hasNext() && notes_.size() < kMaxIndexedNotes) {
        const QString path = it.next();
        const QFileInfo info(path);
        // Skip dotted folders and dotfiles: .git, .trash, editor scratch.
        const QString relative = base.relativeFilePath(info.absoluteFilePath());
        if (relative.startsWith(QLatin1Char('.'))
            || relative.contains(QStringLiteral("/."))) {
            continue;
        }
        VaultNote note;
        note.path = info.absoluteFilePath();
        note.relative = relative;
        note.name = info.completeBaseName();
        notes_.push_back(note);
    }

    std::sort(notes_.begin(), notes_.end(), [](const VaultNote& a, const VaultNote& b) {
        return a.relative.localeAwareCompare(b.relative) < 0;
    });
    for (const VaultNote& note : notes_) {
        byName_[normalizeName(note.name)].append(note.path);
    }

    rewatch();
    emit indexChanged();
}

QString Vault::bestMatch(const QStringList& candidates, const QString& fromPath) const {
    if (candidates.isEmpty()) {
        return {};
    }
    if (candidates.size() == 1) {
        return candidates.first();
    }
    // Prefer a note in the same folder as the document doing the linking, then
    // the shallowest path, then alphabetical order so the answer is stable.
    const QString fromDir =
        fromPath.isEmpty() ? QString() : QFileInfo(fromPath).absolutePath();
    QString best;
    int bestDepth = -1;
    for (const QString& candidate : candidates) {
        if (!fromDir.isEmpty() && QFileInfo(candidate).absolutePath() == fromDir) {
            return candidate;
        }
        const int depth = relativePath(candidate).count(QLatin1Char('/'));
        if (bestDepth < 0 || depth < bestDepth) {
            bestDepth = depth;
            best = candidate;
        }
    }
    return best;
}

QString Vault::resolve(const QString& target, const QString& fromPath, bool wiki) const {
    QString name;
    splitAnchor(target, &name, nullptr);
    if (name.isEmpty()) {
        return {};
    }

    const QString fromDir =
        fromPath.isEmpty() ? QString() : QFileInfo(fromPath).absolutePath();

    // Inside the vault a wiki target is a name, looked up across the whole tree.
    // Everywhere else — including a document that has been moved out of the
    // vault, or opened with the feature switched off — it degrades to a path
    // relative to the document itself, so the link never dies.
    if (wiki && isOpen() && contains(fromPath)) {
        if (name.contains(QLatin1Char('/'))) {
            // Vault-relative, and it must stay inside the vault: a target with
            // ".." segments or a leading slash is not allowed to reach out.
            const QString hit = resolveRelative(name, root_);
            if (!hit.isEmpty() && contains(hit)) {
                return hit;
            }
        }
        if (const QStringList hits = byName_.value(normalizeName(name)); !hits.isEmpty()) {
            return bestMatch(hits, fromPath);
        }
        // A path relative to the document still wins over nothing, as long as it
        // does not escape the vault.
        const QString relative = resolveRelative(name, fromDir);
        return contains(relative) ? relative : QString();
    }

    if (const QString hit = resolveRelative(name, fromDir); !hit.isEmpty()) {
        return hit;
    }
    if (isOpen() && wiki) {
        const QString hit = resolveRelative(name, root_);
        return contains(hit) ? hit : QString();
    }
    return {};
}

QString Vault::newNotePath(const QString& target, const QString& fromPath) const {
    QString name;
    splitAnchor(target, &name, nullptr);
    if (name.isEmpty()) {
        return {};
    }
    // Creating a note must never be able to write outside the place it belongs,
    // so an absolute or ".."-bearing target is refused outright rather than
    // silently clamped into something the user did not ask for.
    if (QFileInfo(name).isAbsolute() || name.startsWith(QLatin1Char('~'))) {
        return {};
    }
    for (const QString& part : name.split(QLatin1Char('/'))) {
        if (part == QLatin1String("..")) {
            return {};
        }
    }
    // A target with a slash is an explicit location; a bare name lands next to
    // the note that referred to it.
    QString dir;
    if (name.contains(QLatin1Char('/'))) {
        dir = isOpen() ? root_ : QFileInfo(fromPath).absolutePath();
    } else if (isOpen() && contains(fromPath)) {
        dir = QFileInfo(fromPath).absolutePath();
    } else if (isOpen()) {
        dir = root_;
    } else if (!fromPath.isEmpty()) {
        dir = QFileInfo(fromPath).absolutePath();
    }
    if (dir.isEmpty()) {
        return {};
    }
    QString file = name;
    if (QFileInfo(file).suffix().isEmpty()) {
        file += QStringLiteral(".md");
    }
    const QString path = QDir(dir).absoluteFilePath(file);
    // Belt and braces: with a vault open the result has to be inside it.
    if (isOpen() && !contains(path)) {
        return {};
    }
    return path;
}

QString Vault::createNote(const QString& dir, const QString& name, QString* error) const {
    const QString trimmed = collapse(name);
    if (dir.isEmpty() || trimmed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("a note needs a name");
        }
        return {};
    }
    if (!isSafeLeafName(trimmed)) {
        if (error) {
            *error = QStringLiteral("a note name cannot contain a path");
        }
        return {};
    }
    if (!QDir().mkpath(dir)) {
        if (error) {
            *error = QStringLiteral("could not create %1").arg(dir);
        }
        return {};
    }

    QString base = trimmed;
    QString suffix = QStringLiteral(".md");
    if (const QString have = QFileInfo(base).suffix().toLower();
        noteSuffixes().contains(have)) {
        suffix = QLatin1Char('.') + QFileInfo(base).suffix();
        base = QFileInfo(base).completeBaseName();
    }
    // Never clobber: "note", "note 2", "note 3"...
    QString path = QDir(dir).absoluteFilePath(base + suffix);
    for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n) {
        path = QDir(dir).absoluteFilePath(base + QStringLiteral(" %1").arg(n) + suffix);
    }
    const QString body = QStringLiteral("# %1\n\n").arg(QFileInfo(path).completeBaseName());
    if (!AtomicFile::write(path, body.toUtf8(), error)) {
        return {};
    }
    return path;
}

QString Vault::createFolder(const QString& dir, const QString& name, QString* error) const {
    const QString trimmed = collapse(name);
    if (dir.isEmpty() || trimmed.isEmpty()) {
        if (error) {
            *error = QStringLiteral("a folder needs a name");
        }
        return {};
    }
    if (!isSafeLeafName(trimmed)) {
        if (error) {
            *error = QStringLiteral("a folder name cannot contain a path");
        }
        return {};
    }
    QString path = QDir(dir).absoluteFilePath(trimmed);
    for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n) {
        path = QDir(dir).absoluteFilePath(trimmed + QStringLiteral(" %1").arg(n));
    }
    if (!QDir().mkpath(path)) {
        if (error) {
            *error = QStringLiteral("could not create %1").arg(path);
        }
        return {};
    }
    return path;
}

QVector<VaultBacklink> Vault::backlinks(const QString& path) const {
    QVector<VaultBacklink> out;
    if (!isOpen() || path.isEmpty()) {
        return out;
    }
    const QString target = QFileInfo(path).absoluteFilePath();

    for (const VaultNote& note : notes_) {
        if (note.path == target) {
            continue;
        }
        QFile file(note.path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        // Reusing the editor's own parser is what keeps a backlink and a
        // clickable link from ever disagreeing: fenced code and nested
        // checkboxes are excluded here for exactly the same reasons.
        int fenceState = StateNone;
        for (int i = 0; i < lines.size(); ++i) {
            const ParseResult parsed = MarkdownRules::parseLine(lines.at(i), fenceState, false);
            fenceState = parsed.nextFenceState;
            for (const LinkRef& link : parsed.links) {
                if (link.target.startsWith(QLatin1Char('#'))) {
                    continue;
                }
                if (resolve(link.target, note.path, link.wiki) != target) {
                    continue;
                }
                VaultBacklink hit;
                hit.path = note.path;
                hit.relative = note.relative;
                hit.name = note.name;
                hit.line = i + 1;
                hit.context = lines.at(i).trimmed();
                out.push_back(hit);
                break; // one row per referring line is enough
            }
        }
    }
    return out;
}
