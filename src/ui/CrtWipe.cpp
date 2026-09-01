#include "ui/CrtWipe.h"

#include <QPainter>
#include <QPropertyAnimation>

CrtWipe::CrtWipe(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void CrtWipe::setTheme(const Theme& theme) {
    theme_ = theme;
}

void CrtWipe::play() {
    progress_ = 0;
    show();
    raise();
    auto* anim = new QPropertyAnimation(this, "progress", this);
    anim->setDuration(220);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    connect(anim, &QPropertyAnimation::finished, this, [this]() { hide(); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CrtWipe::setProgress(qreal value) {
    progress_ = value;
    update();
}

void CrtWipe::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const qreal t = progress_;
    const int alpha = qBound(0, int((1.0 - t) * 220), 220);
    p.fillRect(rect(), QColor(theme_.darkerBackground.red(), theme_.darkerBackground.green(),
                              theme_.darkerBackground.blue(), alpha));
    const int mid = height() / 2;
    const int band = qMax(2, int(height() * t * 0.55));
    QColor beam = theme_.accent;
    beam.setAlpha(qBound(0, int((1.0 - t) * 200), 200));
    p.fillRect(QRect(0, mid - band / 2, width(), qMax(2, band / 8)), beam);
}
