#include "ui/OutlineOverlay.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

OutlineOverlay::OutlineOverlay(QWidget* parent)
    : QWidget(parent)
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("switcherCard"));
    auto* cardLayout = new QVBoxLayout(card);
    filter_->setPlaceholderText(QStringLiteral("jump to heading"));
    cardLayout->addWidget(filter_);
    cardLayout->addWidget(list_);
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();
    card->setFixedWidth(420);
    list_->setFixedHeight(260);
    connect(filter_, &QLineEdit::textChanged, this, &OutlineOverlay::rebuild);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { acceptCurrent(); });
    hide();
}

void OutlineOverlay::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void OutlineOverlay::setChromeFont(const QFont& font) {
    setFont(font);
}

void OutlineOverlay::open(const QVector<OutlineEntry>& entries) {
    entries_ = entries;
    filter_->clear();
    rebuild();
    show();
    raise();
    filter_->setFocus();
}

void OutlineOverlay::rebuild() {
    list_->clear();
    const QString q = filter_->text().trimmed();
    int minLevel = 6;
    for (const OutlineEntry& entry : entries_) {
        minLevel = qMin(minLevel, entry.level);
    }
    for (const OutlineEntry& entry : entries_) {
        if (!q.isEmpty() && !entry.text.contains(q, Qt::CaseInsensitive)) {
            continue;
        }
        const QString indent(qMax(0, entry.level - minLevel) * 2, QLatin1Char(' '));
        auto* item = new QListWidgetItem(indent + entry.text, list_);
        item->setData(Qt::UserRole, entry.blockNumber);
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
}

void OutlineOverlay::acceptCurrent() {
    auto* item = list_->currentItem();
    if (!item) {
        hide();
        return;
    }
    emit chosen(item->data(Qt::UserRole).toInt());
    hide();
}

void OutlineOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        acceptCurrent();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        list_->setCurrentRow(qMin(list_->count() - 1, list_->currentRow() + 1));
        return;
    }
    if (event->key() == Qt::Key_Up) {
        list_->setCurrentRow(qMax(0, list_->currentRow() - 1));
        return;
    }
    QWidget::keyPressEvent(event);
}

void OutlineOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
