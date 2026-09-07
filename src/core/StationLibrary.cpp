#include "core/StationLibrary.h"

#include "core/AtomicFile.h"
#include "core/Paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kSchemaVersion = 1;

QJsonObject toJson(const RadioStation& station) {
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), station.id);
    obj.insert(QStringLiteral("name"), station.name);
    obj.insert(QStringLiteral("url"), station.url);
    obj.insert(QStringLiteral("type"), streamTypeToString(station.type));
    obj.insert(QStringLiteral("genre"), QJsonArray::fromStringList(station.genre));
    obj.insert(QStringLiteral("source"), stationSourceToString(station.source));
    obj.insert(QStringLiteral("favicon"), station.favicon);
    obj.insert(QStringLiteral("isFavorite"), station.isFavorite);
    obj.insert(QStringLiteral("addedAt"), station.addedAt.toUTC().toString(Qt::ISODate));
    // A never-played station keeps a null value rather than an epoch date, so
    // the Recent view can tell "never" from "long ago".
    obj.insert(QStringLiteral("lastPlayedAt"),
               station.lastPlayedAt.isValid() ? station.lastPlayedAt.toUTC().toString(Qt::ISODate)
                                              : QString());
    return obj;
}

RadioStation fromJson(const QJsonObject& obj) {
    RadioStation station;
    station.id = obj.value(QStringLiteral("id")).toString();
    station.name = obj.value(QStringLiteral("name")).toString();
    station.url = obj.value(QStringLiteral("url")).toString();
    station.type = streamTypeFromString(obj.value(QStringLiteral("type")).toString());
    for (const QJsonValue& tag : obj.value(QStringLiteral("genre")).toArray()) {
        const QString text = tag.toString().trimmed();
        if (!text.isEmpty()) {
            station.genre.push_back(text);
        }
    }
    station.source = stationSourceFromString(obj.value(QStringLiteral("source")).toString());
    station.favicon = obj.value(QStringLiteral("favicon")).toString();
    station.isFavorite = obj.value(QStringLiteral("isFavorite")).toBool(false);
    station.addedAt =
        QDateTime::fromString(obj.value(QStringLiteral("addedAt")).toString(), Qt::ISODate);
    station.lastPlayedAt =
        QDateTime::fromString(obj.value(QStringLiteral("lastPlayedAt")).toString(), Qt::ISODate);
    return station;
}

}  // namespace

StationLibrary::StationLibrary(QObject* parent)
    : QObject(parent) {}

void StationLibrary::load() {
    Paths::ensureDirectories();
    stations_ = deserialize(AtomicFile::read(Paths::stationsFile()));
    emit changed();
}

bool StationLibrary::save(QString* error) const {
    Paths::ensureDirectories();
    return AtomicFile::write(Paths::stationsFile(), serialize(stations_).toUtf8(), error);
}

QString StationLibrary::serialize(const QVector<RadioStation>& stations) {
    QJsonArray array;
    for (const RadioStation& station : stations) {
        array.append(toJson(station));
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSchemaVersion);
    root.insert(QStringLiteral("stations"), array);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QVector<RadioStation> StationLibrary::deserialize(const QByteArray& bytes) {
    QVector<RadioStation> stations;
    if (bytes.isEmpty()) {
        return stations;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        return stations;
    }
    for (const QJsonValue& value : doc.object().value(QStringLiteral("stations")).toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const RadioStation station = fromJson(value.toObject());
        // An entry without an id or a URL cannot be played or addressed, so it
        // is dropped rather than kept as a broken row.
        if (station.id.isEmpty() || station.url.isEmpty()) {
            continue;
        }
        stations.push_back(station);
    }
    return stations;
}

int StationLibrary::indexOf(const QString& id) const {
    for (int i = 0; i < stations_.size(); ++i) {
        if (stations_.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

const RadioStation* StationLibrary::byId(const QString& id) const {
    const int index = indexOf(id);
    return index < 0 ? nullptr : &stations_.at(index);
}

const RadioStation* StationLibrary::findByName(const QString& text) const {
    const QString needle = text.trimmed();
    if (needle.isEmpty()) {
        return nullptr;
    }
    // Prefer an exact name before falling back to a substring hit, so a station
    // called "Jazz" wins over "Jazz Radio Classics".
    for (const RadioStation& station : stations_) {
        if (station.name.compare(needle, Qt::CaseInsensitive) == 0) {
            return &station;
        }
    }
    for (const RadioStation& station : stations_) {
        if (station.name.contains(needle, Qt::CaseInsensitive)) {
            return &station;
        }
    }
    return nullptr;
}

QString StationLibrary::add(RadioStation station) {
    if (station.id.isEmpty()) {
        station.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!station.addedAt.isValid()) {
        station.addedAt = QDateTime::currentDateTimeUtc();
    }
    if (station.type == StreamType::Direct) {
        station.type = inferStreamType(station.url);
    }
    stations_.push_back(station);
    save();
    emit changed();
    return station.id;
}

bool StationLibrary::remove(const QString& id) {
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }
    stations_.remove(index);
    save();
    emit changed();
    return true;
}

bool StationLibrary::toggleFavorite(const QString& id) {
    const int index = indexOf(id);
    if (index < 0) {
        return false;
    }
    stations_[index].isFavorite = !stations_.at(index).isFavorite;
    save();
    emit changed();
    return stations_.at(index).isFavorite;
}

void StationLibrary::markPlayed(const QString& id) {
    const int index = indexOf(id);
    if (index < 0) {
        return;
    }
    stations_[index].lastPlayedAt = QDateTime::currentDateTimeUtc();
    save();
    emit changed();
}

QStringList StationLibrary::genres() const {
    QSet<QString> seen;
    for (const RadioStation& station : stations_) {
        for (const QString& tag : station.genre) {
            if (!tag.isEmpty()) {
                seen.insert(tag.toLower());
            }
        }
    }
    QStringList tags(seen.begin(), seen.end());
    tags.sort();
    return tags;
}

QStringList StationLibrary::tokenize(const QString& query) {
    return query.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

bool StationLibrary::matches(const RadioStation& station, const QStringList& tokens) {
    if (tokens.isEmpty()) {
        return true;
    }
    // Substring match over name plus tags, ANDed across tokens, mirroring how
    // the cheat sheet search behaves.
    const QString haystack =
        (station.name + QLatin1Char(' ') + station.genre.join(QLatin1Char(' '))).toLower();
    for (const QString& token : tokens) {
        if (!haystack.contains(token)) {
            return false;
        }
    }
    return true;
}

QVector<RadioStation> StationLibrary::filter(const QVector<RadioStation>& stations, View view,
                                            const QString& query, const QString& genre) {
    const QStringList tokens = tokenize(query);
    QVector<RadioStation> out;
    for (const RadioStation& station : stations) {
        if (view == View::Favorites && !station.isFavorite) {
            continue;
        }
        // Recent only lists what has actually been played.
        if (view == View::Recent && !station.lastPlayedAt.isValid()) {
            continue;
        }
        if (!genre.isEmpty()) {
            bool tagged = false;
            for (const QString& tag : station.genre) {
                if (tag.compare(genre, Qt::CaseInsensitive) == 0) {
                    tagged = true;
                    break;
                }
            }
            if (!tagged) {
                continue;
            }
        }
        if (!matches(station, tokens)) {
            continue;
        }
        out.push_back(station);
    }

    if (view == View::Recent) {
        std::stable_sort(out.begin(), out.end(), [](const RadioStation& a, const RadioStation& b) {
            return a.lastPlayedAt > b.lastPlayedAt;
        });
    }
    return out;
}
