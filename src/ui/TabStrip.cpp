#include "ui/TabStrip.h"

#include "core/Buffer.h"
#include "core/BufferManager.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

TabStrip::TabStrip(BufferManager* buffers, QWidget* parent)
    : QWidget(parent)
    , buffers_(buffers) {
    setFixedHeight(28);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void TabStrip::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void TabStrip::setChromeFont(const QFont& font) {
    QFont f = font;
    f.setPointSizeF(9.5);
    setFont(f);
    update();
}

void TabStrip::setArmedIndex(int index) {
    if (armed_ == index) {
        return;
    }
    armed_ = index;
    update();
}

void TabStrip::setComparePair(int left, int right) {
    if (compareLeft_ == left && compareRight_ == right) {
        return;
    }
    compareLeft_ = left;
    compareRight_ = right;
    update();
}

QSize TabStrip::sizeHint() const {
    return {200, 28};
}

int TabStrip::tabWidth() const {
    if (buffers_->count() <= 0) {
        return width();
    }
    const int available = qMax(80, width() - 8);
    return qBound(72, available / buffers_->count(), 220);
}

QRect TabStrip::tabRect(int index) const {
    const int w = tabWidth();
    return QRect(4 + index * w, 0, w, height());
}

int TabStrip::tabAt(const QPoint& pos) const {
    for (int i = 0; i < buffers_->count(); ++i) {
        if (tabRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void TabStrip::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), theme_.darkBackground);
    p.setFont(font());
    const QFontMetrics fm(font());
    for (int i = 0; i < buffers_->count(); ++i) {
        const Buffer* buf = buffers_->at(i);
        const QRect r = tabRect(i);
        const bool current = (i == buffers_->currentIndex());
        const bool compared = (i == compareLeft_ || i == compareRight_);
        const bool armed = (i == armed_);
        if (current || compared) {
            p.fillRect(r, theme_.background);
            p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 2), theme_.accent);
        } else if (armed) {
            p.fillRect(r, theme_.background);
            p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 2), theme_.muted);
        } else if (i == hover_) {
            p.fillRect(r, theme_.lighterBackground);
        }
        QString title = buf->title();
        if (buf->isDirty()) {
            title.prepend(QStringLiteral("● "));
        }
        title = fm.elidedText(title, Qt::ElideRight, r.width() - 16);
        p.setPen((current || compared || armed) ? theme_.brightForeground : theme_.darkForeground);
        p.drawText(r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, title);
    }
    p.setPen(theme_.muted);
    p.drawLine(0, height() - 1, width(), height() - 1);
}

void TabStrip::mousePressEvent(QMouseEvent* event) {
    const int index = tabAt(event->pos());
    if (index < 0) {
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        emit tabCloseRequested(index);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            emit tabShiftClicked(index);
            return;
        }
        dragFrom_ = index;
        emit tabClicked(index);
    }
}

void TabStrip::mouseMoveEvent(QMouseEvent* event) {
    hover_ = tabAt(event->pos());
    update();
    if (dragFrom_ < 0 || !(event->buttons() & Qt::LeftButton)) {
        return;
    }
    const int to = tabAt(event->pos());
    if (to >= 0 && to != dragFrom_) {
        emit tabMoved(dragFrom_, to);
        dragFrom_ = to;
    }
}

void TabStrip::mouseReleaseEvent(QMouseEvent*) {
    dragFrom_ = -1;
}

void TabStrip::mouseDoubleClickEvent(QMouseEvent* event) {
    const int index = tabAt(event->pos());
    if (index >= 0) {
        emit tabRenameRequested(index);
    }
}
