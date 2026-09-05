#include "ui/PdfExportOverlay.h"

#include "core/PdfExport.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

PdfExportOverlay::PdfExportOverlay(QWidget* parent)
    : QWidget(parent)
    , title_(new QLabel(QStringLiteral("export pdf"), this))
    , hint_(new QLabel(QStringLiteral("enter: export   esc: close"), this))
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("switcherCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(8);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(title_);
    header->addStretch();
    header->addWidget(hint_);
    cardLayout->addLayout(header);

    filter_->setPlaceholderText(QStringLiteral("template"));
    cardLayout->addWidget(filter_);
    cardLayout->addWidget(list_);

    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();
    card->setFixedWidth(460);
    list_->setFixedHeight(220);

    connect(filter_, &QLineEdit::textChanged, this, &PdfExportOverlay::rebuild);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { acceptCurrent(); });
    restyle();
    hide();
}

void PdfExportOverlay::setTheme(const Theme& theme) {
    theme_ = theme;
    restyle();
    update();
}

void PdfExportOverlay::setChromeFont(const QFont& font) {
    setFont(font);
    filter_->setFont(font);
    list_->setFont(font);
    hint_->setFont(font);
    QFont titleFont = font;
    titleFont.setPointSizeF(font.pointSizeF() + 2);
    title_->setFont(titleFont);
}

void PdfExportOverlay::restyle() {
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.accent.name()));
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.muted.name()));
}

void PdfExportOverlay::open(const QString& currentId) {
    currentId_ = PdfTemplates::normalize(currentId);
    filter_->clear();
    rebuild();
    show();
    raise();
    filter_->setFocus();
}

void PdfExportOverlay::rebuild() {
    list_->clear();
    const QString q = filter_->text().trimmed();
    int select = 0;
    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        const QString hay = tpl.id + QLatin1Char(' ') + tpl.blurb;
        if (!q.isEmpty() && !hay.contains(q, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(tpl.id, tpl.blurb), list_);
        item->setData(Qt::UserRole, tpl.id);
        if (tpl.id == currentId_) {
            select = list_->count() - 1;
        }
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(select);
    }
}

void PdfExportOverlay::acceptCurrent() {
    auto* item = list_->currentItem();
    if (!item) {
        hide();
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    hide();
    emit chosen(id);
}

void PdfExportOverlay::keyPressEvent(QKeyEvent* event) {
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

void PdfExportOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
