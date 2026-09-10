#include "ui/VaultOverlay.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

namespace {

constexpr int kCardWidth = 520;
constexpr int kListHeight = 300;
constexpr int kPathRole = Qt::UserRole;
constexpr int kLineRole = Qt::UserRole + 1;

// Every query token has to appear somewhere in the row, same rule the cheat
// sheet uses, so searching behaves consistently across overlays.
bool matches(const QString& haystack, const QStringList& tokens) {
    for (const QString& token : tokens) {
        if (!haystack.contains(token)) {
            return false;
        }
    }
    return true;
}

} // namespace

VaultOverlay::VaultOverlay(QWidget* parent)
    : QWidget(parent)
    , title_(new QLabel(this))
    , filter_(new QLineEdit(this))
    , list_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("switcherCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->addWidget(title_);
    cardLayout->addWidget(filter_);
    cardLayout->addWidget(list_);
    layout->addWidget(card, 0, Qt::AlignHCenter);
    layout->addStretch();

    card->setFixedWidth(kCardWidth);
    list_->setFixedHeight(kListHeight);
    list_->setAlternatingRowColors(false);

    connect(filter_, &QLineEdit::textChanged, this, &VaultOverlay::rebuild);
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { acceptCurrent(); });
    hide();
}

void VaultOverlay::setTheme(const Theme& theme) {
    theme_ = theme;
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.accent.name()));
    update();
}

void VaultOverlay::setChromeFont(const QFont& font) {
    setFont(font);
    filter_->setFont(font);
    list_->setFont(font);
    title_->setFont(font);
}

void VaultOverlay::present(const QString& title, const QString& placeholder) {
    title_->setText(title);
    filter_->setPlaceholderText(placeholder);
    filter_->clear();
    rebuild();
    show();
    raise();
    filter_->setFocus();
}

void VaultOverlay::openNotes(const QVector<VaultNote>& notes) {
    mode_ = Mode::Notes;
    rows_.clear();
    rows_.reserve(notes.size());
    for (const VaultNote& note : notes) {
        Row row;
        row.label = note.name;
        // The folder, when the note is not at the top level: enough to tell two
        // same-named notes apart without turning the list into paths.
        const int slash = note.relative.lastIndexOf(QLatin1Char('/'));
        row.detail = slash > 0 ? note.relative.left(slash) : QString();
        row.path = note.path;
        rows_.push_back(row);
    }
    present(QStringLiteral("notes"), QStringLiteral("open a note"));
}

void VaultOverlay::openBacklinks(const QVector<VaultBacklink>& links, const QString& title) {
    mode_ = Mode::Backlinks;
    rows_.clear();
    rows_.reserve(links.size());
    for (const VaultBacklink& link : links) {
        Row row;
        row.label = link.name;
        row.detail = link.context;
        row.path = link.path;
        row.line = link.line;
        rows_.push_back(row);
    }
    const QString heading = links.isEmpty()
                                ? QStringLiteral("no links to %1").arg(title)
                                : QStringLiteral("links to %1").arg(title);
    present(heading, QStringLiteral("filter backlinks"));
}

void VaultOverlay::openVaults(const QVector<QPair<QString, QString>>& vaults,
                              const QString& activeRoot) {
    mode_ = Mode::Vaults;
    rows_.clear();
    rows_.reserve(vaults.size());
    for (const auto& vault : vaults) {
        Row row;
        row.label = vault.first;
        row.detail = vault.second;
        row.path = vault.second;
        rows_.push_back(row);
    }
    present(QStringLiteral("vaults"), QStringLiteral("switch vault"));
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(kPathRole).toString() == activeRoot) {
            list_->setCurrentRow(i);
            break;
        }
    }
}

void VaultOverlay::rebuild() {
    list_->clear();
    QStringList tokens;
    for (const QString& part :
         filter_->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        tokens.append(part.toLower());
    }

    for (const Row& row : rows_) {
        const QString haystack = (row.label + QLatin1Char(' ') + row.detail).toLower();
        if (!matches(haystack, tokens)) {
            continue;
        }
        QString text = row.label;
        if (!row.detail.isEmpty()) {
            if (mode_ == Mode::Backlinks) {
                text += QStringLiteral("  %1: %2").arg(row.line).arg(row.detail);
            } else {
                text += QStringLiteral("   %1").arg(row.detail);
            }
        }
        auto* item = new QListWidgetItem(text, list_);
        item->setData(kPathRole, row.path);
        item->setData(kLineRole, row.line);
        item->setToolTip(row.path);
    }
    if (list_->count() > 0 && !list_->currentItem()) {
        list_->setCurrentRow(0);
    }
}

void VaultOverlay::acceptCurrent() {
    auto* item = list_->currentItem();
    if (!item) {
        hide();
        return;
    }
    const QString path = item->data(kPathRole).toString();
    const int line = item->data(kLineRole).toInt();
    hide();
    if (path.isEmpty()) {
        return;
    }
    if (mode_ == Mode::Vaults) {
        emit vaultChosen(path);
        return;
    }
    emit pathChosen(path, line);
}

void VaultOverlay::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        // Clear the query first, then close: same two-step Esc as the cheat sheet.
        if (!filter_->text().isEmpty()) {
            filter_->clear();
            return;
        }
        hide();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        acceptCurrent();
        return;
    case Qt::Key_Down:
        list_->setCurrentRow(qMin(list_->count() - 1, list_->currentRow() + 1));
        return;
    case Qt::Key_Up:
        list_->setCurrentRow(qMax(0, list_->currentRow() - 1));
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void VaultOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
