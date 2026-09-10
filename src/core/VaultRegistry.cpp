#include "core/VaultRegistry.h"

#include "core/AtomicFile.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

VaultRegistry::VaultRegistry(QObject* parent)
    : QObject(parent) {}

QString VaultRegistry::normalize(const QString& root) {
    QString text = root.trimmed();
    if (text.isEmpty()) {
        return {};
    }
    if (text.startsWith(QLatin1Char('~'))) {
        text = QDir::homePath() + text.mid(1);
    }
    return QFileInfo(text).absoluteFilePath();
}

void VaultRegistry::load() {
    entries_ = deserialize(AtomicFile::read(Paths::vaultsFile()));
    emit changed();
}

bool VaultRegistry::save(QString* error) const {
    Paths::ensureDirectories();
    return AtomicFile::write(Paths::vaultsFile(), serialize(entries_).toUtf8(), error);
}

int VaultRegistry::indexOf(const QString& root) const {
    const QString needle = normalize(root);
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).root == needle) {
            return i;
        }
    }
    return -1;
}

QString VaultRegistry::remember(const QString& root) {
    const QString path = normalize(root);
    if (path.isEmpty()) {
        return {};
    }
    const int at = indexOf(path);
    VaultEntry entry = at >= 0 ? entries_.at(at) : VaultEntry{};
    entry.root = path;
    if (entry.name.trimmed().isEmpty()) {
        entry.name = QDir(path).dirName();
    }
    entry.lastOpenedAt = QDateTime::currentSecsSinceEpoch();
    if (at >= 0) {
        entries_.removeAt(at);
    }
    entries_.prepend(entry);
    save();
    emit changed();
    return path;
}

bool VaultRegistry::forget(const QString& root) {
    const int at = indexOf(root);
    if (at < 0) {
        return false;
    }
    entries_.removeAt(at);
    save();
    emit changed();
    return true;
}

QString VaultRegistry::mostRecent() const {
    for (const VaultEntry& entry : entries_) {
        if (QFileInfo(entry.root).isDir()) {
            return entry.root;
        }
    }
    return {};
}

QString VaultRegistry::serialize(const QVector<VaultEntry>& entries) {
    QJsonArray array;
    for (const VaultEntry& entry : entries) {
        if (entry.root.isEmpty()) {
            continue;
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("root"), entry.root);
        obj.insert(QStringLiteral("name"), entry.name);
        obj.insert(QStringLiteral("lastOpenedAt"), entry.lastOpenedAt);
        array.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("vaults"), array);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QVector<VaultEntry> VaultRegistry::deserialize(const QByteArray& bytes) {
    QVector<VaultEntry> out;
    if (bytes.isEmpty()) {
        return out;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    // Tolerate a bare array as well as the versioned object, so a hand-edited
    // file keeps working.
    const QJsonArray array = doc.isArray() ? doc.array()
                                           : doc.object().value(QStringLiteral("vaults")).toArray();
    for (const QJsonValue& value : array) {
        const QJsonObject obj = value.toObject();
        VaultEntry entry;
        entry.root = normalize(obj.value(QStringLiteral("root")).toString());
        if (entry.root.isEmpty()) {
            continue;
        }
        entry.name = obj.value(QStringLiteral("name")).toString();
        if (entry.name.trimmed().isEmpty()) {
            entry.name = QDir(entry.root).dirName();
        }
        entry.lastOpenedAt = qint64(obj.value(QStringLiteral("lastOpenedAt")).toDouble(0));
        bool duplicate = false;
        for (const VaultEntry& seen : out) {
            duplicate = duplicate || seen.root == entry.root;
        }
        if (!duplicate) {
            out.push_back(entry);
        }
    }
    return out;
}
