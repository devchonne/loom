#pragma once

#include <QString>
#include <QVector>

struct BufferState {
    QString id;
    QString title;
    QString path;
    QString text;
    int cursor = 0;
    int scroll = 0;
    bool dirty = false;
};

struct Session {
    int active = 0;
    int zoom = 100;
    QByteArray geometry;
    bool zen = false;
    int nextScratch = 1;
    QVector<BufferState> buffers;
};

class SessionStore {
public:
    static Session load();
    static bool save(const Session& session, QString* error = nullptr);
    static bool writeScratch(const QString& id, const QString& text, QString* error = nullptr);
    static QString readScratch(const QString& id);
    static void removeScratch(const QString& id);
};
