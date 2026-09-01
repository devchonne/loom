#include "ui/FindBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

FindBar::FindBar(QWidget* parent)
    : QWidget(parent)
    , input_(new QLineEdit(this)) {
    setObjectName(QStringLiteral("findBar"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(8);
    auto* label = new QLabel(QStringLiteral("find"), this);
    auto* next = new QPushButton(QStringLiteral("n"), this);
    auto* prev = new QPushButton(QStringLiteral("N"), this);
    auto* close = new QPushButton(QStringLiteral("esc"), this);
    next->setFixedWidth(36);
    prev->setFixedWidth(36);
    close->setFixedWidth(48);
    layout->addWidget(label);
    layout->addWidget(input_, 1);
    layout->addWidget(prev);
    layout->addWidget(next);
    layout->addWidget(close);
    connect(input_, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(prev, &QPushButton::clicked, this, &FindBar::findPrev);
    connect(close, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });
    hide();
}

void FindBar::setTheme(const Theme&) {}

void FindBar::setChromeFont(const QFont& font) {
    setFont(font);
}

void FindBar::open(const QString& seed) {
    show();
    if (!seed.isEmpty()) {
        input_->setText(seed);
    }
    input_->setFocus();
    input_->selectAll();
}

QString FindBar::query() const {
    return input_->text();
}

void FindBar::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    if (event->key() == Qt::Key_G && event->modifiers() & Qt::ControlModifier) {
        if (event->modifiers() & Qt::ShiftModifier) {
            emit findPrev();
        } else {
            emit findNext();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}
