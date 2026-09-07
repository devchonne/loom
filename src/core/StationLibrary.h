#pragma once

#include "core/RadioStation.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

// The user's own saved stations: the only radio data that is persisted, and the
// only thing playback ever reads from. Crawler results are ephemeral until the
// user explicitly adds one, at which point it lands here like any manual entry.
//
// Backed by a flat JSON array in ~/.local/state/loom/stations.json, written
// through AtomicFile the same way the session is.
class StationLibrary : public QObject {
    Q_OBJECT

public:
    // Which slice of the library a view wants. Favorites and Recent are their
    // own tabs rather than filters the user has to re-apply each time.
    enum class View : std::uint8_t { All, Favorites, Recent };

    explicit StationLibrary(QObject* parent = nullptr);

    void load();
    bool save(QString* error = nullptr) const;

    // Assigns an id and addedAt, persists, and emits changed(). Both the manual
    // and the crawler add paths converge here; only the UI differs.
    QString add(RadioStation station);
    bool remove(const QString& id);
    bool toggleFavorite(const QString& id);
    // Stamps lastPlayedAt so the station surfaces in the Recent view.
    void markPlayed(const QString& id);

    const QVector<RadioStation>& stations() const { return stations_; }
    bool isEmpty() const { return stations_.isEmpty(); }
    int count() const { return int(stations_.size()); }
    const RadioStation* byId(const QString& id) const;
    // First station whose name contains the text, for `/radio play <name>`.
    const RadioStation* findByName(const QString& text) const;

    // Every tag in use across the library, de-duplicated and sorted, for the
    // genre filter. Safe because the vocabulary is constrained.
    QStringList genres() const;

    // Pure helpers, kept static so the filtering rules are unit testable
    // without touching the disk.
    static QStringList tokenize(const QString& query);
    static bool matches(const RadioStation& station, const QStringList& tokens);
    static QVector<RadioStation> filter(const QVector<RadioStation>& stations, View view,
                                        const QString& query, const QString& genre);

    static QString serialize(const QVector<RadioStation>& stations);
    static QVector<RadioStation> deserialize(const QByteArray& bytes);

signals:
    void changed();

private:
    int indexOf(const QString& id) const;

    QVector<RadioStation> stations_;
};
