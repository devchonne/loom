#include "core/SessionStore.h"

#include "core/AtomicFile.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

Session SessionStore::load() {
    Session session;
    Paths::ensureDirectories();
    const QByteArray bytes = AtomicFile::read(Paths::sessionFile());
    if (bytes.isEmpty()) {
        return session;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        return session;
    }
    const QJsonObject root = doc.object();
    session.active = root.value(QStringLiteral("active")).toInt(0);
    session.zoom = root.value(QStringLiteral("zoom")).toInt(100);
    session.zen = root.value(QStringLiteral("zen")).toBool(false);
    session.nextScratch = root.value(QStringLiteral("nextScratch")).toInt(1);
    session.geometry = QByteArray::fromBase64(root.value(QStringLiteral("geometry")).toString().toLatin1());

    const QJsonArray buffers = root.value(QStringLiteral("buffers")).toArray();
    for (const QJsonValue& value : buffers) {
        const QJsonObject obj = value.toObject();
        BufferState state;
        state.id = obj.value(QStringLiteral("id")).toString();
        state.title = obj.value(QStringLiteral("title")).toString();
        state.path = obj.value(QStringLiteral("path")).toString();
        state.cursor = obj.value(QStringLiteral("cursor")).toInt(0);
        state.scroll = obj.value(QStringLiteral("scroll")).toInt(0);
        state.dirty = obj.value(QStringLiteral("dirty")).toBool(false);
        if (state.id.isEmpty()) {
            continue;
        }

        const QString scratch = readScratch(state.id);
        if (!state.path.isEmpty() && QFile::exists(state.path) && !state.dirty) {
            QFile file(state.path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                state.text = QString::fromUtf8(file.readAll());
            }
        } else if (!scratch.isEmpty() || QFile::exists(Paths::scratchFile(state.id))) {
            state.text = scratch;
        } else if (!state.path.isEmpty() && QFile::exists(state.path)) {
            QFile file(state.path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                state.text = QString::fromUtf8(file.readAll());
            }
        }
        session.buffers.push_back(state);
    }
    return session;
}

bool SessionStore::save(const Session& session, QString* error) {
    Paths::ensureDirectories();
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("active"), session.active);
    root.insert(QStringLiteral("zoom"), session.zoom);
    root.insert(QStringLiteral("zen"), session.zen);
    root.insert(QStringLiteral("nextScratch"), session.nextScratch);
    root.insert(QStringLiteral("geometry"), QString::fromLatin1(session.geometry.toBase64()));

    QJsonArray buffers;
    for (const BufferState& state : session.buffers) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), state.id);
        obj.insert(QStringLiteral("title"), state.title);
        obj.insert(QStringLiteral("path"), state.path);
        obj.insert(QStringLiteral("cursor"), state.cursor);
        obj.insert(QStringLiteral("scroll"), state.scroll);
        obj.insert(QStringLiteral("dirty"), state.dirty);
        buffers.append(obj);
        if (!writeScratch(state.id, state.text, error)) {
            return false;
        }
    }
    root.insert(QStringLiteral("buffers"), buffers);

    const QJsonDocument doc(root);
    return AtomicFile::write(Paths::sessionFile(), doc.toJson(QJsonDocument::Indented), error);
}

bool SessionStore::writeScratch(const QString& id, const QString& text, QString* error) {
    if (id.isEmpty()) {
        return true;
    }
    return AtomicFile::write(Paths::scratchFile(id), text.toUtf8(), error);
}

QString SessionStore::readScratch(const QString& id) {
    if (id.isEmpty()) {
        return {};
    }
    const QByteArray bytes = AtomicFile::read(Paths::scratchFile(id));
    return QString::fromUtf8(bytes);
}

void SessionStore::removeScratch(const QString& id) {
    if (id.isEmpty()) {
        return;
    }
    QFile::remove(Paths::scratchFile(id));
}
