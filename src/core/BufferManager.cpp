#include "core/BufferManager.h"

#include "core/Buffer.h"

#include <QFileInfo>

BufferManager::BufferManager(QObject* parent)
    : QObject(parent) {}

Buffer* BufferManager::current() const {
    if (current_ < 0 || current_ >= buffers_.size()) {
        return nullptr;
    }
    return buffers_[current_];
}

Buffer* BufferManager::at(int index) const {
    if (index < 0 || index >= buffers_.size()) {
        return nullptr;
    }
    return buffers_[index];
}

int BufferManager::indexOf(Buffer* buffer) const {
    return buffers_.indexOf(buffer);
}

Buffer* BufferManager::makeBuffer(const QString& id) {
    auto* buffer = id.isEmpty() ? new Buffer(this) : new Buffer(id, this);
    wire(buffer);
    return buffer;
}

void BufferManager::wire(Buffer* buffer) {
    connect(buffer, &Buffer::titleChanged, this, &BufferManager::structureChanged);
    connect(buffer, &Buffer::dirtyChanged, this, &BufferManager::structureChanged);
    connect(buffer, &Buffer::contentsChanged, this, &BufferManager::contentsChanged);
}

Buffer* BufferManager::createScratch() {
    Buffer* buffer = makeBuffer({});
    buffer->setTitle(QStringLiteral("scratch-%1").arg(nextScratch_++));
    const int index = buffers_.size();
    buffers_.push_back(buffer);
    emit structureChanged();
    setCurrentIndex(index);
    return buffer;
}

Buffer* BufferManager::openPath(const QString& path, const QString& text) {
    for (int i = 0; i < buffers_.size(); ++i) {
        if (!path.isEmpty() && buffers_[i]->path() == path) {
            setCurrentIndex(i);
            return buffers_[i];
        }
    }
    Buffer* buffer = makeBuffer({});
    buffer->setPath(path);
    buffer->setTitle(path.isEmpty() ? QStringLiteral("scratch-%1").arg(nextScratch_++)
                                    : QFileInfo(path).fileName());
    buffer->setText(text, true);
    const int index = buffers_.size();
    buffers_.push_back(buffer);
    emit structureChanged();
    setCurrentIndex(index);
    return buffer;
}

void BufferManager::closeAt(int index) {
    if (index < 0 || index >= buffers_.size()) {
        return;
    }
    Buffer* buffer = buffers_.takeAt(index);
    buffer->deleteLater();
    if (buffers_.isEmpty()) {
        current_ = -1;
        createScratch();
        return;
    }
    int next = current_;
    if (index < current_) {
        next = current_ - 1;
    } else if (index == current_) {
        next = qMin(index, buffers_.size() - 1);
    }
    current_ = -1;
    emit structureChanged();
    setCurrentIndex(next);
}

void BufferManager::setCurrentIndex(int index) {
    if (index < 0 || index >= buffers_.size() || index == current_) {
        return;
    }
    emit aboutToChangeCurrent();
    current_ = index;
    emit currentChanged(index);
}

void BufferManager::moveTab(int from, int to) {
    if (from < 0 || from >= buffers_.size() || to < 0 || to >= buffers_.size() || from == to) {
        return;
    }
    buffers_.move(from, to);
    if (current_ == from) {
        current_ = to;
    } else if (from < current_ && to >= current_) {
        --current_;
    } else if (from > current_ && to <= current_) {
        ++current_;
    }
    emit structureChanged();
    emit currentChanged(current_);
}

void BufferManager::restore(const Session& session) {
    qDeleteAll(buffers_);
    buffers_.clear();
    current_ = -1;
    nextScratch_ = qMax(1, session.nextScratch);
    for (const BufferState& state : session.buffers) {
        Buffer* buffer = makeBuffer(state.id);
        buffer->setTitle(state.title.isEmpty() ? QStringLiteral("scratch") : state.title);
        buffer->setPath(state.path);
        buffer->setCursor(state.cursor);
        buffer->setScroll(state.scroll);
        buffer->setText(state.text, !state.dirty);
        if (state.dirty) {
            buffer->markDirty();
        }
        buffers_.push_back(buffer);
        bool ok = false;
        const int n = state.title.section(QLatin1Char('-'), 1).toInt(&ok);
        if (ok) {
            nextScratch_ = qMax(nextScratch_, n + 1);
        }
    }
    if (buffers_.isEmpty()) {
        createScratch();
        return;
    }
    emit structureChanged();
    const int active = qBound(0, session.active, buffers_.size() - 1);
    setCurrentIndex(active);
}

Session BufferManager::snapshot(int zoom, const QByteArray& geometry, bool zen) const {
    Session session;
    session.active = qMax(0, current_);
    session.zoom = zoom;
    session.geometry = geometry;
    session.zen = zen;
    session.nextScratch = nextScratch_;
    for (Buffer* buffer : buffers_) {
        BufferState state;
        state.id = buffer->id();
        state.title = buffer->title();
        state.path = buffer->path();
        state.text = buffer->text();
        state.cursor = buffer->cursor();
        state.scroll = buffer->scroll();
        state.dirty = buffer->isDirty();
        session.buffers.push_back(state);
    }
    return session;
}
