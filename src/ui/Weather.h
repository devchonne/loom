#pragma once

#include "theme/Theme.h"

#include <QSize>
#include <QVector>

class QPainter;
class QRect;

enum class WeatherMode { Off, Rain, Storm };

class Weather {
public:
    void setMode(WeatherMode mode);
    WeatherMode mode() const { return mode_; }

    void setTheme(const Theme& theme);
    void resize(const QSize& size);
    void tick();
    void paint(QPainter& painter, const QRect& rect) const;

private:
    struct Drop {
        float x = 0;
        float y = 0;
        float length = 8;
        float speed = 3;
        float wind = 0.1f;
        float alpha = 16;
        float tint = 0;
    };

    void rebuild();
    void spawn(Drop& drop, bool scatterY);

    WeatherMode mode_ = WeatherMode::Off;
    Theme theme_ = Theme::builtin();
    QSize size_;
    QVector<Drop> drops_;
    int lightningCooldown_ = 120;
    int lightningFrames_ = 0;
    float flash_ = 0;
};
