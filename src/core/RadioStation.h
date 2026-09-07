#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <cstdint>

// How the stream URL has to be handled before it reaches the audio player.
// Direct URLs go straight in; playlist files have to be fetched and parsed
// first to find the real stream (see PlaylistResolver).
enum class StreamType : std::uint8_t { Direct, Pls, M3u, M3u8 };

// Where a library entry came from. The player never cares: once a station is
// saved it is just a name plus a URL.
enum class StationSource : std::uint8_t { Manual, Crawler };

// One saved entry in the user's local station library.
struct RadioStation {
    QString id;
    QString name;
    QString url;
    StreamType type = StreamType::Direct;
    // Constrained to the directory's tag vocabulary rather than free text, so
    // filtering stays consistent no matter how the station was added.
    QStringList genre;
    StationSource source = StationSource::Manual;
    // Kept from crawler metadata but never rendered; the lists stay text-only.
    QString favicon;
    bool isFavorite = false;
    QDateTime addedAt;
    // Null until the station has actually been played once.
    QDateTime lastPlayedAt;
};

// Infers the stream type from the URL's file extension, ignoring any query
// string. Anything unrecognised is treated as a direct stream.
StreamType inferStreamType(const QString& url);

QString streamTypeToString(StreamType type);
StreamType streamTypeFromString(const QString& text);

QString stationSourceToString(StationSource source);
StationSource stationSourceFromString(const QString& text);
