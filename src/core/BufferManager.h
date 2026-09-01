#pragma once

#include "core/SessionStore.h"

#include <QObject>
#include <QVector>

class Buffer;

class BufferManager : public QObject {
    Q_OBJECT

public:
    explicit BufferManager(QObject* parent = nullptr);

    int count() const { return buffers_.size(); }
    int currentIndex() const { return current_; }
    int nextScratch() const { return nextScratch_; }
    Buffer* current() const;
    Buffer* at(int index) const;
    int indexOf(Buffer* buffer) const;

    Buffer* createScratch();
    Buffer* openPath(const QString& path, const QString& text);
    void closeAt(int index);
    void setCurrentIndex(int index);
    void moveTab(int from, int to);
    void restore(const Session& session);
    Session snapshot(int zoom, const QByteArray& geometry, bool zen) const;

signals:
    void aboutToChangeCurrent();
    void currentChanged(int index);
    void structureChanged();
    void contentsChanged();

private:
    Buffer* makeBuffer(const QString& id);
    void wire(Buffer* buffer);

    QVector<Buffer*> buffers_;
    int current_ = -1;
    int nextScratch_ = 1;
};
