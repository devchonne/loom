#include "ui/RadioOverlay.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace {

constexpr int kCardWidth = 700;

QString joinTags(const QStringList& tags) {
    return tags.isEmpty() ? QStringLiteral("untagged") : tags.join(QStringLiteral(", "));
}

}  // namespace

RadioOverlay::RadioOverlay(StationLibrary* library, RadioDirectory* directory, QWidget* parent)
    : QWidget(parent)
    , library_(library)
    , directory_(directory)
    , card_(new QWidget(this))
    , title_(new QLabel(QStringLiteral("radio"), this))
    , hint_(new QLabel(this))
    , status_(new QLabel(this))
    , pages_(new QStackedWidget(this))
    , search_(new QLineEdit(this))
    , tabs_(new QTabBar(this))
    , genre_(new QComboBox(this))
    , list_(new QListWidget(this))
    , mini_(new QCheckBox(QStringLiteral("show mini player"), this))
    , addName_(new QLineEdit(this))
    , addUrl_(new QLineEdit(this))
    , addTags_(new QListWidget(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();

    card_->setObjectName(QStringLiteral("switcherCard"));
    card_->setFixedWidth(kCardWidth);
    auto* cardLayout = new QVBoxLayout(card_);
    cardLayout->setSpacing(8);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(title_);
    header->addStretch();
    header->addWidget(hint_);
    cardLayout->addLayout(header);

    // --- library page ---
    auto* libraryPage = new QWidget(card_);
    auto* libraryLayout = new QVBoxLayout(libraryPage);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(8);

    search_->setPlaceholderText(QStringLiteral("search stations"));
    search_->installEventFilter(this);
    libraryLayout->addWidget(search_);

    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setFocusPolicy(Qt::NoFocus);
    tabs_->addTab(QStringLiteral("all"));
    tabs_->addTab(QStringLiteral("favorites"));
    tabs_->addTab(QStringLiteral("recent"));
    tabs_->addTab(QStringLiteral("browse"));

    auto* filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(tabs_);
    filterRow->addStretch();
    genre_->setFocusPolicy(Qt::NoFocus);
    genre_->setMinimumWidth(160);
    filterRow->addWidget(genre_);
    libraryLayout->addLayout(filterRow);

    list_->setFocusPolicy(Qt::NoFocus);
    list_->setUniformItemSizes(true);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setFixedHeight(300);
    libraryLayout->addWidget(list_);
    pages_->addWidget(libraryPage);

    // --- add page ---
    auto* addPage = new QWidget(card_);
    auto* addLayout = new QVBoxLayout(addPage);
    addLayout->setContentsMargins(0, 0, 0, 0);
    addLayout->setSpacing(8);
    addName_->setPlaceholderText(QStringLiteral("name"));
    addName_->installEventFilter(this);
    addUrl_->setPlaceholderText(QStringLiteral("stream url"));
    addUrl_->installEventFilter(this);
    addLayout->addWidget(addName_);
    addLayout->addWidget(addUrl_);
    // Genres are picked from the directory's vocabulary, never typed, so tags
    // stay consistent with crawler-added stations.
    addTags_->setSelectionMode(QAbstractItemView::MultiSelection);
    addTags_->setFocusPolicy(Qt::NoFocus);
    addTags_->setUniformItemSizes(true);
    addTags_->setFixedHeight(240);
    addLayout->addWidget(addTags_);
    pages_->addWidget(addPage);

    cardLayout->addWidget(pages_);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->addWidget(status_, 1);
    footer->addWidget(mini_);
    cardLayout->addLayout(footer);

    layout->addWidget(card_, 0, Qt::AlignHCenter);
    layout->addStretch();

    connect(search_, &QLineEdit::textChanged, this, [this]() {
        if (currentTab() == Tab::Browse) {
            runBrowseQuery();
            return;
        }
        rebuild();
    });
    connect(tabs_, &QTabBar::currentChanged, this, [this](int) {
        if (currentTab() == Tab::Browse) {
            runBrowseQuery();
        }
        rebuild();
    });
    connect(genre_, &QComboBox::currentIndexChanged, this, [this](int) { rebuild(); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { activateCurrent(); });
    connect(mini_, &QCheckBox::toggled, this, [this](bool on) { emit miniPlayerToggled(on); });

    if (library_) {
        connect(library_, &StationLibrary::changed, this, [this]() {
            refreshGenreFilter();
            rebuild();
        });
    }
    if (directory_) {
        connect(directory_, &RadioDirectory::stationsReady, this,
                [this](const QVector<DirectoryStation>& results) {
                    browseResults_ = results;
                    if (currentTab() == Tab::Browse) {
                        rebuild();
                    }
                });
        connect(directory_, &RadioDirectory::tagsReady, this, [this]() { refreshTagPicker(); });
        connect(directory_, &RadioDirectory::failed, this,
                [this](const QString& text) { setStatusMessage(text); });
    }

    refreshGenreFilter();
    refreshTagPicker();
    restyle();
    updateHint();
    rebuild();
    hide();
}

void RadioOverlay::setTheme(const Theme& theme) {
    theme_ = theme;
    restyle();
    rebuild();
    update();
}

void RadioOverlay::setChromeFont(const QFont& font) {
    setFont(font);
    hint_->setFont(font);
    status_->setFont(font);
    search_->setFont(font);
    tabs_->setFont(font);
    genre_->setFont(font);
    list_->setFont(font);
    mini_->setFont(font);
    addName_->setFont(font);
    addUrl_->setFont(font);
    addTags_->setFont(font);
    QFont titleFont = font;
    titleFont.setPointSizeF(font.pointSizeF() + 2);
    title_->setFont(titleFont);
    update();
}

void RadioOverlay::restyle() {
    title_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.accent.name()));
    hint_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.muted.name()));
    status_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.muted.name()));
    mini_->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.muted.name()));
    tabs_->setStyleSheet(QStringLiteral(R"(
QTabBar::tab {
    background: %1;
    color: %2;
    border: 1px solid %1;
    padding: 3px 10px;
    margin-right: 2px;
}
QTabBar::tab:hover {
    color: %3;
}
QTabBar::tab:selected {
    background: %4;
    color: %5;
    border-color: %3;
}
)")
                              .arg(theme_.darkerBackground.name(),
                                   theme_.muted.name(),
                                   theme_.accent.name(),
                                   theme_.selection.name(),
                                   theme_.brightForeground.name()));
}

void RadioOverlay::updateHint() {
    if (mode_ == Mode::Add) {
        hint_->setText(QStringLiteral("enter: save   esc: back"));
        title_->setText(QStringLiteral("add station"));
        return;
    }
    title_->setText(QStringLiteral("radio"));
    hint_->setText(currentTab() == Tab::Browse
                       ? QStringLiteral("enter: add   tab: section   esc: close")
                       : QStringLiteral("enter: play   alt+f: favorite   alt+d: remove   alt+a: add"));
}

RadioOverlay::Tab RadioOverlay::currentTab() const {
    switch (tabs_->currentIndex()) {
    case 1:
        return Tab::Favorites;
    case 2:
        return Tab::Recent;
    case 3:
        return Tab::Browse;
    default:
        break;
    }
    return Tab::All;
}

void RadioOverlay::setMode(Mode mode) {
    mode_ = mode;
    pages_->setCurrentIndex(mode_ == Mode::Add ? 1 : 0);
    updateHint();
    if (mode_ == Mode::Add) {
        addName_->setFocus();
    } else {
        search_->setFocus();
    }
}

void RadioOverlay::setPlayingStation(const QString& id) {
    if (playingId_ == id) {
        return;
    }
    playingId_ = id;
    rebuild();
}

void RadioOverlay::setStatusMessage(const QString& text) {
    status_->setText(text);
}

void RadioOverlay::setMiniPlayerEnabled(bool enabled) {
    const QSignalBlocker block(mini_);
    mini_->setChecked(enabled);
}

void RadioOverlay::open() {
    setMode(Mode::Library);
    search_->clear();
    refreshGenreFilter();
    rebuild();
    show();
    raise();
    search_->setFocus();
    // Scroll straight to whatever is playing, so reopening from the mini player
    // lands on the right row.
    if (!playingId_.isEmpty()) {
        for (int i = 0; i < shown_.size(); ++i) {
            if (shown_.at(i).id == playingId_) {
                list_->setCurrentRow(i);
                list_->scrollToItem(list_->item(i));
                break;
            }
        }
    }
}

void RadioOverlay::openAdd() {
    show();
    raise();
    addName_->clear();
    addUrl_->clear();
    addTags_->clearSelection();
    if (directory_ && directory_->tags().isEmpty()) {
        directory_->fetchTags();
    }
    setMode(Mode::Add);
}

void RadioOverlay::dismiss() {
    if (mode_ == Mode::Add) {
        setMode(Mode::Library);
        return;
    }
    if (!search_->text().isEmpty()) {
        search_->clear();
        return;
    }
    hide();
}

void RadioOverlay::refreshGenreFilter() {
    if (!library_) {
        return;
    }
    const QString previous = genre_->currentIndex() > 0 ? genre_->currentText() : QString();
    const QSignalBlocker block(genre_);
    genre_->clear();
    genre_->addItem(QStringLiteral("all genres"));
    for (const QString& tag : library_->genres()) {
        genre_->addItem(tag);
    }
    if (!previous.isEmpty()) {
        const int index = genre_->findText(previous);
        genre_->setCurrentIndex(index > 0 ? index : 0);
    }
}

void RadioOverlay::refreshTagPicker() {
    if (!directory_) {
        return;
    }
    const QSignalBlocker block(addTags_);
    addTags_->clear();
    for (const QString& tag : directory_->tags()) {
        addTags_->addItem(tag);
    }
}

void RadioOverlay::rebuild() {
    if (currentTab() == Tab::Browse) {
        rebuildBrowseList();
    } else {
        rebuildLibraryList();
    }
    updateHint();
}

void RadioOverlay::rebuildLibraryList() {
    if (!library_) {
        return;
    }
    const int previousRow = list_->currentRow();
    list_->clear();
    browseResults_.clear();

    StationLibrary::View view = StationLibrary::View::All;
    if (currentTab() == Tab::Favorites) {
        view = StationLibrary::View::Favorites;
    } else if (currentTab() == Tab::Recent) {
        view = StationLibrary::View::Recent;
    }
    const QString genre = genre_->currentIndex() > 0 ? genre_->currentText() : QString();
    shown_ = StationLibrary::filter(library_->stations(), view, search_->text().trimmed(), genre);

    genre_->setEnabled(true);
    for (const RadioStation& station : shown_) {
        const bool playing = !playingId_.isEmpty() && station.id == playingId_;
        // A leading marker keeps the row icon-free while still showing state.
        const QString marker = playing ? QStringLiteral("▶ ")
                                       : (station.isFavorite ? QStringLiteral("★ ")
                                                             : QStringLiteral("  "));
        auto* item = new QListWidgetItem(
            QStringLiteral("%1%2  ·  %3").arg(marker, station.name, joinTags(station.genre)), list_);
        if (playing) {
            item->setForeground(theme_.accent);
        } else if (station.isFavorite) {
            item->setForeground(theme_.brightForeground);
        } else {
            item->setForeground(theme_.foreground);
        }
    }

    if (list_->count() > 0) {
        list_->setCurrentRow(qBound(0, previousRow, list_->count() - 1));
    }
    if (shown_.isEmpty()) {
        const bool empty = library_->isEmpty();
        setStatusMessage(empty ? QStringLiteral("no stations yet — press alt+a to add one")
                               : QStringLiteral("no matches"));
    }
}

void RadioOverlay::rebuildBrowseList() {
    const int previousRow = list_->currentRow();
    list_->clear();
    shown_.clear();
    // Genre is a library-only filter; browse narrows through the directory.
    genre_->setEnabled(false);

    // Crowdsourced data goes stale, so anything that failed its last check
    // sorts last and is drawn in the error colour rather than silently trusted.
    QVector<DirectoryStation> ordered;
    for (const DirectoryStation& station : browseResults_) {
        if (station.lastCheckOk) {
            ordered.push_back(station);
        }
    }
    for (const DirectoryStation& station : browseResults_) {
        if (!station.lastCheckOk) {
            ordered.push_back(station);
        }
    }
    browseResults_ = ordered;

    for (const DirectoryStation& station : browseResults_) {
        QStringList parts{station.name, joinTags(station.tags)};
        if (!station.countryCode.isEmpty()) {
            parts.push_back(station.countryCode.toLower());
        }
        if (station.bitrate > 0) {
            parts.push_back(QStringLiteral("%1k").arg(station.bitrate));
        }
        if (!station.lastCheckOk) {
            parts.push_back(QStringLiteral("offline?"));
        }
        auto* item = new QListWidgetItem(parts.join(QStringLiteral("  ·  ")), list_);
        item->setForeground(station.lastCheckOk ? theme_.foreground : theme_.red);
    }

    if (list_->count() > 0) {
        list_->setCurrentRow(qBound(0, previousRow, list_->count() - 1));
    } else {
        setStatusMessage(QStringLiteral("searching the directory…"));
    }
}

void RadioOverlay::runBrowseQuery() {
    if (!directory_) {
        return;
    }
    const QString query = search_->text().trimmed();
    if (query.isEmpty()) {
        // Browsing without searching: the directory's most-clicked stations.
        directory_->topClicked();
        return;
    }
    directory_->searchByName(query);
}

void RadioOverlay::activateCurrent() {
    const int row = list_->currentRow();
    if (row < 0) {
        return;
    }
    if (currentTab() == Tab::Browse) {
        if (row >= browseResults_.size() || !library_) {
            return;
        }
        // Copy the resolved fields across; nothing was stored until now.
        const DirectoryStation& found = browseResults_.at(row);
        RadioStation station;
        station.name = found.name;
        station.url = found.url;
        station.type = inferStreamType(found.url);
        station.genre = found.tags;
        station.source = StationSource::Crawler;
        station.favicon = found.favicon;
        library_->add(station);
        setStatusMessage(QStringLiteral("added %1").arg(found.name));
        return;
    }
    if (row >= shown_.size()) {
        return;
    }
    emit playRequested(shown_.at(row).id);
}

void RadioOverlay::favoriteCurrent() {
    const int row = list_->currentRow();
    if (currentTab() == Tab::Browse || row < 0 || row >= shown_.size() || !library_) {
        return;
    }
    library_->toggleFavorite(shown_.at(row).id);
}

void RadioOverlay::removeCurrent() {
    const int row = list_->currentRow();
    if (currentTab() == Tab::Browse || row < 0 || row >= shown_.size() || !library_) {
        return;
    }
    const RadioStation station = shown_.at(row);
    // Removing what is playing would leave the mini player pointing at nothing.
    if (station.id == playingId_) {
        emit stopRequested();
    }
    library_->remove(station.id);
    setStatusMessage(QStringLiteral("removed %1").arg(station.name));
}

void RadioOverlay::submitAdd() {
    if (!library_) {
        return;
    }
    const QString name = addName_->text().trimmed();
    const QString url = addUrl_->text().trimmed();
    if (name.isEmpty() || url.isEmpty()) {
        setStatusMessage(QStringLiteral("name and url are both required"));
        return;
    }
    RadioStation station;
    station.name = name;
    station.url = url;
    station.type = inferStreamType(url);
    station.source = StationSource::Manual;
    for (const QListWidgetItem* item : addTags_->selectedItems()) {
        station.genre.push_back(item->text());
    }
    library_->add(station);
    setStatusMessage(QStringLiteral("added %1").arg(name));
    setMode(Mode::Library);
}

void RadioOverlay::moveSelection(int delta) {
    if (list_->count() == 0) {
        return;
    }
    const int row = qBound(0, list_->currentRow() + delta, list_->count() - 1);
    list_->setCurrentRow(row);
    list_->scrollToItem(list_->item(row));
}

void RadioOverlay::cycleTab(int delta) {
    const int n = tabs_->count();
    if (n == 0) {
        return;
    }
    tabs_->setCurrentIndex((tabs_->currentIndex() + delta + n) % n);
}

bool RadioOverlay::handleNavKey(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (mode_ == Mode::Add) {
            submitAdd();
        } else {
            activateCurrent();
        }
        return true;
    }
    if (mode_ == Mode::Add) {
        // Let the form's line edits behave normally otherwise.
        return false;
    }

    switch (event->key()) {
    case Qt::Key_Tab:
        cycleTab(1);
        return true;
    case Qt::Key_Backtab:
        cycleTab(-1);
        return true;
    case Qt::Key_Down:
        moveSelection(1);
        return true;
    case Qt::Key_Up:
        moveSelection(-1);
        return true;
    case Qt::Key_PageDown:
        moveSelection(10);
        return true;
    case Qt::Key_PageUp:
        moveSelection(-10);
        return true;
    default:
        break;
    }

    // Row actions are Alt-modified rather than bare letters: the search field
    // keeps focus the whole time, so a plain "a" has to stay searchable. Ctrl is
    // no good either, since the window-level shortcuts fire through overlays.
    if (event->modifiers().testFlag(Qt::AltModifier)) {
        switch (event->key()) {
        case Qt::Key_A:
            openAdd();
            return true;
        case Qt::Key_F:
            favoriteCurrent();
            return true;
        case Qt::Key_D:
            removeCurrent();
            return true;
        default:
            break;
        }
    }
    return false;
}

bool RadioOverlay::eventFilter(QObject* watched, QEvent* event) {
    const bool watched_field = watched == search_ || watched == addName_ || watched == addUrl_;
    if (watched_field && event->type() == QEvent::KeyPress) {
        if (handleNavKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RadioOverlay::keyPressEvent(QKeyEvent* event) {
    if (handleNavKey(event)) {
        return;
    }
    QWidget::keyPressEvent(event);
}

void RadioOverlay::mousePressEvent(QMouseEvent* event) {
    if (card_ && card_->geometry().contains(event->pos())) {
        QWidget::mousePressEvent(event);
        return;
    }
    hide();
}

void RadioOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Keep the card inside small windows / zen mode instead of clipping it.
    card_->setFixedWidth(qMax(360, qMin(kCardWidth, width() - 48)));
    const int listHeight = qBound(160, height() - 260, 520);
    list_->setFixedHeight(listHeight);
    addTags_->setFixedHeight(listHeight - 60);
}

void RadioOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}
