#include "ui/WeatherSound.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <QtEndian>
#include <QtGlobal>

#include <cstring>
#include <utility>

namespace {

class LoopPcmDevice final : public QIODevice {
public:
    explicit LoopPcmDevice(QByteArray pcm, QObject* parent = nullptr)
        : QIODevice(parent)
        , pcm_(std::move(pcm)) {}

    bool isSequential() const override { return true; }
    bool atEnd() const override { return false; }
    qint64 bytesAvailable() const override { return qint64(pcm_.size()) + QIODevice::bytesAvailable(); }

    void rewind() { offset_ = 0; }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        if (pcm_.isEmpty() || maxlen <= 0) {
            return 0;
        }
        qint64 written = 0;
        while (written < maxlen) {
            const qint64 remain = qint64(pcm_.size()) - offset_;
            const qint64 chunk = qMin(maxlen - written, remain);
            std::memcpy(data + written, pcm_.constData() + offset_, size_t(chunk));
            written += chunk;
            offset_ += chunk;
            if (offset_ >= qint64(pcm_.size())) {
                offset_ = 0;
            }
        }
        return written;
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    QByteArray pcm_;
    qint64 offset_ = 0;
};

bool loadWavPcm(const QByteArray& bytes, QAudioFormat* format, QByteArray* pcm) {
    if (bytes.size() < 44) {
        return false;
    }
    const auto* p = reinterpret_cast<const uchar*>(bytes.constData());
    auto u16 = [&](int o) { return qFromLittleEndian<quint16>(p + o); };
    auto u32 = [&](int o) { return qFromLittleEndian<quint32>(p + o); };
    if (std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) {
        return false;
    }

    int pos = 12;
    bool haveFmt = false;
    while (pos + 8 <= bytes.size()) {
        const quint32 size = u32(pos + 4);
        const char* id = bytes.constData() + pos;
        pos += 8;
        if (pos + int(size) > bytes.size()) {
            return false;
        }
        if (std::memcmp(id, "fmt ", 4) == 0) {
            if (size < 16) {
                return false;
            }
            const quint16 audioFormat = u16(pos);
            const quint16 channels = u16(pos + 2);
            const quint32 rate = u32(pos + 4);
            const quint16 bits = u16(pos + 14);
            if (audioFormat != 1 || bits != 16 || channels < 1) {
                return false;
            }
            format->setSampleRate(int(rate));
            format->setChannelCount(int(channels));
            format->setSampleFormat(QAudioFormat::Int16);
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            *pcm = bytes.mid(pos, int(size));
        }
        pos += int((size + 1) & ~quint32(1));
    }
    return haveFmt && !pcm->isEmpty();
}

} // namespace

WeatherSound::WeatherSound(QObject* parent)
    : QObject(parent) {
    QFile in(QStringLiteral(":/sounds/rain_fx.wav"));
    if (!in.open(QIODevice::ReadOnly)) {
        qWarning("loom: missing bundled rain clip :/sounds/rain_fx.wav");
        return;
    }

    QAudioFormat format;
    QByteArray pcm;
    if (!loadWavPcm(in.readAll(), &format, &pcm)) {
        qWarning("loom: rain clip is not 16-bit PCM WAV");
        return;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        qWarning("loom: no audio output device");
        return;
    }

    sink_ = new QAudioSink(device, format, this);
    sink_->setBufferSize(qMax(format.bytesForDuration(200000), 4096));
    auto* loop = new LoopPcmDevice(std::move(pcm), this);
    loop->open(QIODevice::ReadOnly);
    loop_ = loop;

    connect(sink_, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (starting_ || !shouldPlay()) {
            return;
        }
        if (state == QAudio::IdleState) {
            QTimer::singleShot(0, this, [this]() {
                if (shouldPlay() && sink_ && sink_->state() != QAudio::ActiveState) {
                    startSink();
                }
            });
        } else if (state == QAudio::StoppedState && sink_->error() != QAudio::NoError) {
            qWarning("loom rain audio: sink error %d", int(sink_->error()));
        }
    });
}

void WeatherSound::setWanted(bool wanted) {
    wanted_ = wanted;
    sync();
}

void WeatherSound::setWeather(WeatherMode mode) {
    weather_ = mode;
    applyVolume();
    sync();
}

bool WeatherSound::shouldPlay() const {
    return wanted_ && weather_ != WeatherMode::Off && sink_ && loop_;
}

void WeatherSound::applyVolume() {
    if (!sink_) {
        return;
    }
    sink_->setVolume(weather_ == WeatherMode::Storm ? 1.0f : 0.85f);
}

void WeatherSound::startSink() {
    if (!sink_ || !loop_ || starting_) {
        return;
    }
    starting_ = true;
    stopSink();
    static_cast<LoopPcmDevice*>(loop_)->rewind();
    sink_->start(loop_);
    starting_ = false;
}

void WeatherSound::stopSink() {
    if (!sink_) {
        return;
    }
    if (sink_->state() != QAudio::StoppedState) {
        sink_->stop();
    }
}

void WeatherSound::sync() {
    if (shouldPlay()) {
        if (sink_->state() != QAudio::ActiveState) {
            startSink();
        }
    } else {
        stopSink();
    }
}
