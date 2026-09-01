#include "ui/Weather.h"

#include <QPainter>
#include <QPen>
#include <QRandomGenerator>

#include <cmath>

namespace {
QColor lerp(const QColor& a, const QColor& b, float t) {
    t = qBound(0.f, t, 1.f);
    return QColor(qRound(a.red() + (b.red() - a.red()) * t),
                  qRound(a.green() + (b.green() - a.green()) * t),
                  qRound(a.blue() + (b.blue() - a.blue()) * t));
}

int targetCount(WeatherMode mode) {
    switch (mode) {
    case WeatherMode::Rain:
        return 80;
    case WeatherMode::Storm:
        return 200;
    case WeatherMode::Off:
        break;
    }
    return 0;
}
}

void Weather::setMode(WeatherMode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    rebuild();
}

void Weather::setTheme(const Theme& theme) {
    theme_ = theme;
}

void Weather::resize(const QSize& size) {
    size_ = size;
}

void Weather::rebuild() {
    drops_.resize(targetCount(mode_));
    for (Drop& drop : drops_) {
        spawn(drop, true);
    }
    flash_ = 0;
    lightningFrames_ = 0;
    lightningCooldown_ = 90 + QRandomGenerator::global()->bounded(150);
}

void Weather::spawn(Drop& drop, bool scatterY) {
    QRandomGenerator* rng = QRandomGenerator::global();
    const float w = float(qMax(1, size_.width()));
    const float h = float(qMax(1, size_.height()));
    const bool storm = mode_ == WeatherMode::Storm;

    float t = std::pow(float(rng->generateDouble()), 1.8f);
    if (rng->bounded(2) == 0) {
        t = 1.f - t;
    }
    drop.x = t * w;

    drop.length = storm ? float(rng->bounded(12, 34)) : float(rng->bounded(8, 20));
    drop.speed = storm ? (6.f + float(rng->generateDouble()) * 5.f)
                       : (2.5f + float(rng->generateDouble()) * 2.f);
    drop.wind = storm ? (0.4f + float(rng->generateDouble()) * 0.8f)
                      : (0.05f + float(rng->generateDouble()) * 0.15f);
    const int extra = theme_.dark ? 0 : 10;
    drop.alpha = storm ? float(32 + rng->bounded(18) + extra) : float(24 + rng->bounded(14) + extra);
    drop.tint = float(rng->generateDouble());

    if (scatterY) {
        drop.y = float(rng->generateDouble()) * h;
    } else {
        drop.y = -drop.length - float(rng->generateDouble()) * 48.f;
        drop.x += drop.wind * float(rng->generateDouble()) * 12.f;
    }
}

void Weather::tick() {
    if (mode_ == WeatherMode::Off) {
        return;
    }

    const float w = float(qMax(1, size_.width()));
    const float h = float(qMax(1, size_.height()));
    for (Drop& drop : drops_) {
        drop.x += drop.wind;
        drop.y += drop.speed;
        if (drop.y - drop.length > h || drop.x < -24.f || drop.x > w + 24.f) {
            spawn(drop, false);
        }
    }

    if (mode_ != WeatherMode::Storm) {
        flash_ = 0;
        lightningFrames_ = 0;
        return;
    }

    if (lightningFrames_ > 0) {
        --lightningFrames_;
        flash_ = lightningFrames_ > 2 ? 1.f : 0.35f;
        if (lightningFrames_ == 0) {
            flash_ = 0;
            lightningCooldown_ = 90 + QRandomGenerator::global()->bounded(150);
        }
        return;
    }

    --lightningCooldown_;
    if (lightningCooldown_ <= 0) {
        lightningFrames_ = 3 + QRandomGenerator::global()->bounded(3);
        flash_ = 1.f;
    }
}

void Weather::paint(QPainter& painter, const QRect& rect) const {
    QColor fill = theme_.background;
    if (flash_ > 0.001f) {
        const QColor bolt = lerp(theme_.blue, theme_.lighterBackground, 0.35f);
        fill = lerp(theme_.background, bolt, flash_ * 0.28f);
    }
    painter.fillRect(rect, fill);

    if (drops_.isEmpty()) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const Drop& drop : drops_) {
        const float mag = std::hypot(drop.wind, drop.speed);
        const float ux = mag > 0.001f ? drop.wind / mag : 0.f;
        const float uy = mag > 0.001f ? drop.speed / mag : 1.f;
        QColor color = lerp(theme_.cyan, lerp(theme_.blue, theme_.muted, 0.2f), drop.tint);
        int alpha = qRound(drop.alpha);
        if (!theme_.dark) {
            alpha = qBound(0, alpha + 12, 80);
        }
        color.setAlpha(alpha);
        painter.setPen(QPen(color, 1));
        painter.drawLine(QPointF(drop.x, drop.y),
                         QPointF(drop.x - ux * drop.length, drop.y - uy * drop.length));
    }
}
