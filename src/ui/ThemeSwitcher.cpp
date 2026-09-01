#include "ui/ThemeSwitcher.h"

#include "theme/Theme.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

ThemeSwitcher::ThemeSwitcher(QWidget* parent)
    : QWidget(parent)
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("switcherCard"));
    auto* cardLayout = new QVBoxLayout(card);
    filter_->setPlaceholderText(QStringLiteral("theme"));
    cardLayout->addWidget(filter_);
    cardLayout->addWidget(list_);
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();
    card->setFixedWidth(420);
    list_->setFixedHeight(280);
    connect(filter_, &QLineEdit::textChanged, this, &ThemeSwitcher::rebuild);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { acceptCurrent(); });
    hide();
}

void ThemeSwitcher::setTheme(const Theme& theme) {
    theme_ = theme;
    update();
}

void ThemeSwitcher::setChromeFont(const QFont& font) {
    setFont(font);
}

void ThemeSwitcher::open(const QString& currentId) {
    currentId_ = Palettes::normalize(currentId);
    filter_->clear();
    rebuild();
    show();
    raise();
    filter_->setFocus();
}

void ThemeSwitcher::rebuild() {
    list_->clear();
    const QString q = filter_->text().trimmed();
    const auto themes = Palettes::catalog();
    int select = 0;
    for (const ThemeSpec& spec : themes) {
        const QString hay = spec.name + QLatin1Char(' ') + spec.group + QLatin1Char(' ') + spec.id;
        if (!q.isEmpty() && !hay.contains(q, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(spec.name, spec.group), list_);
        item->setData(Qt::UserRole, spec.id);
        if (spec.id == currentId_) {
            select = list_->count() - 1;
        }
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(select);
    }
}

void ThemeSwitcher::acceptCurrent() {
    auto* item = list_->currentItem();
    if (!item) {
        hide();
        return;
    }
    emit chosen(item->data(Qt::UserRole).toString());
    hide();
}

void ThemeSwitcher::keyPressEvent(QKeyEvent* event) {
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

void ThemeSwitcher::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
