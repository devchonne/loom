#pragma once

#include "ui/Weather.h"

#include <QObject>

class QAudioSink;
class QIODevice;

class WeatherSound : public QObject {
    Q_OBJECT

public:
    explicit WeatherSound(QObject* parent = nullptr);

    void setWanted(bool wanted);
    bool wanted() const { return wanted_; }
    void setWeather(WeatherMode mode);

private:
    void sync();
    bool shouldPlay() const;
    void startSink();
    void stopSink();
    void applyVolume();

    QAudioSink* sink_ = nullptr;
    QIODevice* loop_ = nullptr;
    bool wanted_ = false;
    bool starting_ = false;
    WeatherMode weather_ = WeatherMode::Off;
};
