#pragma once

#include "core/RadioStation.h"

#include <QObject>
#include <QString>

#include <cstdint>

class QAudioOutput;
class QMediaPlayer;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// What the player is doing right now, which is also what the mini player draws.
enum class RadioState : std::uint8_t { Idle, Connecting, Playing, Paused, Reconnecting, Error };

// Drives playback of one station at a time through a single QMediaPlayer.
// Switching stations swaps the source rather than opening a second stream.
//
// Note this deliberately does not reuse WeatherSound, which decodes a bundled
// PCM WAV into a looping QIODevice behind a QAudioSink: that path cannot handle
// a network stream, its codecs, or its buffering.
class RadioPlayer : public QObject {
    Q_OBJECT

public:
    explicit RadioPlayer(QObject* parent = nullptr);

    void play(const RadioStation& station);
    void pause();
    void resume();
    void toggle();
    // Tears the source down entirely rather than pausing, so the mini player
    // returns to its idle state and nothing keeps buffering.
    void stop();

    void setVolume(double volume);
    double volume() const { return volume_; }

    RadioState state() const { return state_; }
    bool isActive() const { return state_ != RadioState::Idle; }
    QString currentStationId() const { return station_.id; }
    QString currentStationName() const { return station_.name; }

signals:
    void stateChanged(RadioState state);
    void stationChanged(const QString& id);
    // Human-readable status for the dialog's footer line.
    void message(const QString& text);
    // Emitted the first time a station actually starts playing, so the library
    // can stamp lastPlayedAt.
    void started(const QString& id);

private:
    void setState(RadioState state);
    // Fetches and parses a .pls/.m3u before handing the real URL to the player.
    void resolvePlaylist(const QString& url);
    void beginStream(const QString& url);
    void scheduleRetry(const QString& reason);
    void cancelRetry();
    void abortPlaylistFetch();

    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audio_ = nullptr;
    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* playlistReply_ = nullptr;
    QTimer* retry_ = nullptr;
    RadioStation station_;
    // Stream URL actually handed to QMediaPlayer, which differs from the
    // station's URL for playlist types.
    QString resolvedUrl_;
    RadioState state_ = RadioState::Idle;
    double volume_ = 0.8;
    int retries_ = 0;
    bool announcedStart_ = false;
};
