#include "ui/TabSwitcher.h"

#include "core/Buffer.h"
#include "core/BufferManager.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

TabSwitcher::TabSwitcher(BufferManager* buffers, QWidget* parent)
    : QWidget(parent)
    , buffers_(buffers)
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(width() > 0 ? 0 : 0, 0, 0, 0);
    layout->addStretch();
    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("switcherCard"));
    auto* cardLayout = new QVBoxLayout(card);
    filter_->setPlaceholderText(QStringLiteral("switch tab"));
    cardLayout->addWidget(filter_);
    cardLayout->addWidget(list_);
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();
    card->setFixedWidth(420);
    list_->setFixedHeight(220);
    connect(filter_, &QLineEdit::textChanged, this, &TabSwitcher::rebuild);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { acceptCurrent(); });
    hide();
}

void TabSwitcher::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void TabSwitcher::setChromeFont(const QFont& font) {
    setFont(font);
}

void TabSwitcher::open() {
    filter_->clear();
    rebuild();
    show();
    raise();
    filter_->setFocus();
}

void TabSwitcher::rebuild() {
    list_->clear();
    const QString q = filter_->text().trimmed();
    for (int i = 0; i < buffers_->count(); ++i) {
        const Buffer* buf = buffers_->at(i);
        const QString title = buf->title();
        if (!q.isEmpty() && !title.contains(q, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(title, list_);
        item->setData(Qt::UserRole, i);
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
}

void TabSwitcher::acceptCurrent() {
    auto* item = list_->currentItem();
    if (!item) {
        hide();
        return;
    }
    emit chosen(item->data(Qt::UserRole).toInt());
    hide();
}

void TabSwitcher::keyPressEvent(QKeyEvent* event) {
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

void TabSwitcher::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
