#include "core/RadioPlayer.h"

#include "core/PlaylistResolver.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

// Streams drop; back off rather than hammering a dead server, and give up after
// three tries so the error state is honest.
constexpr int kMaxRetries = 3;
constexpr int kFirstRetryMs = 2000;

}  // namespace

RadioPlayer::RadioPlayer(QObject* parent)
    : QObject(parent)
    , player_(new QMediaPlayer(this))
    , audio_(new QAudioOutput(this))
    , net_(new QNetworkAccessManager(this))
    , retry_(new QTimer(this)) {
    player_->setAudioOutput(audio_);
    audio_->setVolume(float(volume_));
    retry_->setSingleShot(true);

    connect(retry_, &QTimer::timeout, this, [this]() {
        if (resolvedUrl_.isEmpty()) {
            return;
        }
        setState(RadioState::Reconnecting);
        player_->setSource(QUrl(resolvedUrl_));
        player_->play();
    });

    connect(player_, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState playback) {
                if (playback == QMediaPlayer::PlayingState) {
                    cancelRetry();
                    setState(RadioState::Playing);
                    if (!announcedStart_) {
                        announcedStart_ = true;
                        emit started(station_.id);
                    }
                    emit message(station_.name);
                } else if (playback == QMediaPlayer::PausedState) {
                    setState(RadioState::Paused);
                }
            });

    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (state_ == RadioState::Idle) {
                    return;
                }
                switch (status) {
                case QMediaPlayer::LoadingMedia:
                case QMediaPlayer::BufferingMedia:
                    if (state_ != RadioState::Reconnecting) {
                        setState(RadioState::Connecting);
                    }
                    break;
                case QMediaPlayer::StalledMedia:
                    scheduleRetry(QStringLiteral("reconnecting…"));
                    break;
                case QMediaPlayer::EndOfMedia:
                    // A live stream should not end; treat it as a drop.
                    scheduleRetry(QStringLiteral("stream ended, reconnecting…"));
                    break;
                default:
                    break;
                }
            });

    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString& text) {
                if (error == QMediaPlayer::NoError || state_ == RadioState::Idle) {
                    return;
                }
                scheduleRetry(text.isEmpty() ? QStringLiteral("stream error") : text);
            });
}

void RadioPlayer::setState(RadioState state) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_);
}

void RadioPlayer::setVolume(double volume) {
    volume_ = qBound(0.0, volume, 1.0);
    audio_->setVolume(float(volume_));
}

void RadioPlayer::play(const RadioStation& station) {
    if (station.url.isEmpty()) {
        setState(RadioState::Error);
        emit message(QStringLiteral("station has no url"));
        return;
    }
    // HLS is out of scope for v1: say so instead of failing opaquely.
    if (station.type == StreamType::M3u8) {
        station_ = station;
        emit stationChanged(station_.id);
        setState(RadioState::Error);
        emit message(QStringLiteral("hls streams are not supported yet"));
        return;
    }

    cancelRetry();
    abortPlaylistFetch();
    retries_ = 0;
    announcedStart_ = false;
    station_ = station;
    resolvedUrl_.clear();
    emit stationChanged(station_.id);
    setState(RadioState::Connecting);
    emit message(QStringLiteral("connecting to %1…").arg(station_.name));

    if (station_.type == StreamType::Pls || station_.type == StreamType::M3u) {
        resolvePlaylist(station_.url);
        return;
    }
    beginStream(station_.url);
}

void RadioPlayer::resolvePlaylist(const QString& url) {
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", QStringLiteral("loom/%1")
                                       .arg(QCoreApplication::applicationVersion())
                                       .toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = net_->get(req);
    playlistReply_ = reply;
    const StreamType type = station_.type;
    connect(reply, &QNetworkReply::finished, this, [this, reply, type]() {
        if (reply != playlistReply_) {
            reply->deleteLater();
            return;
        }
        playlistReply_ = nullptr;
        const QNetworkReply::NetworkError error = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (error == QNetworkReply::OperationCanceledError) {
            return;
        }
        if (error != QNetworkReply::NoError) {
            setState(RadioState::Error);
            emit message(QStringLiteral("station unreachable"));
            return;
        }
        const QString stream = PlaylistResolver::firstStreamUrl(type, body);
        if (stream.isEmpty()) {
            setState(RadioState::Error);
            emit message(QStringLiteral("no stream found in playlist"));
            return;
        }
        beginStream(stream);
    });
}

void RadioPlayer::beginStream(const QString& url) {
    resolvedUrl_ = url;
    player_->setSource(QUrl(url));
    player_->play();
}

void RadioPlayer::scheduleRetry(const QString& reason) {
    if (resolvedUrl_.isEmpty()) {
        return;
    }
    if (retries_ >= kMaxRetries) {
        cancelRetry();
        setState(RadioState::Error);
        emit message(QStringLiteral("station unreachable"));
        return;
    }
    // 2s, 4s, 8s.
    const int delay = kFirstRetryMs << retries_;
    ++retries_;
    setState(RadioState::Reconnecting);
    emit message(reason);
    retry_->start(delay);
}

void RadioPlayer::cancelRetry() {
    retry_->stop();
    retries_ = 0;
}

void RadioPlayer::abortPlaylistFetch() {
    if (!playlistReply_) {
        return;
    }
    QNetworkReply* reply = playlistReply_;
    playlistReply_ = nullptr;
    reply->abort();
    reply->deleteLater();
}

void RadioPlayer::pause() {
    if (state_ == RadioState::Idle) {
        return;
    }
    cancelRetry();
    player_->pause();
    setState(RadioState::Paused);
}

void RadioPlayer::resume() {
    if (station_.url.isEmpty()) {
        return;
    }
    if (resolvedUrl_.isEmpty()) {
        // Stopped earlier, so the source is gone: start the station over.
        play(station_);
        return;
    }
    player_->play();
}

void RadioPlayer::toggle() {
    switch (state_) {
    case RadioState::Playing:
    case RadioState::Connecting:
    case RadioState::Reconnecting:
        pause();
        return;
    case RadioState::Paused:
    case RadioState::Error:
        resume();
        return;
    case RadioState::Idle:
        if (!station_.url.isEmpty()) {
            play(station_);
        }
        return;
    }
}

void RadioPlayer::stop() {
    cancelRetry();
    abortPlaylistFetch();
    player_->stop();
    // Fully tear the source down, not just pause, so nothing keeps buffering.
    player_->setSource(QUrl());
    resolvedUrl_.clear();
    announcedStart_ = false;
    setState(RadioState::Idle);
    emit message(QString());
}
