#pragma once

#include "core/RadioDirectory.h"
#include "core/RadioPlayer.h"
#include "core/StationLibrary.h"
#include "theme/Theme.h"

#include <QVector>
#include <QWidget>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTabBar;

// The radio library and browse dialog: Ctrl+Alt+R, /radio.
//
// Follows the same recipe as the other overlays: a translucent scrim over the
// central widget with one centred "switcherCard" holding everything, so it
// inherits the app's chrome instead of looking like a bolted-on window.
class RadioOverlay : public QWidget {
    Q_OBJECT

public:
    RadioOverlay(StationLibrary* library, RadioDirectory* directory, QWidget* parent = nullptr);

    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);

    void open();
    // Opens with the manual-add form already showing, for `/radio add`.
    void openAdd();
    // Clears the query first, closes on a second call, mirroring CheatSheet.
    void dismiss();

    void setPlayingStation(const QString& id);
    void setStatusMessage(const QString& text);
    void setMiniPlayerEnabled(bool enabled);

signals:
    void playRequested(const QString& id);
    void stopRequested();
    void miniPlayerToggled(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The card shows one of two things: the saved library (with a browse tab) or
    // the manual-add form.
    enum class Mode : std::uint8_t { Library, Add };
    // Tab order in the library view.
    enum class Tab : std::uint8_t { All, Favorites, Recent, Browse };

    void setMode(Mode mode);
    Tab currentTab() const;
    void rebuild();
    void rebuildLibraryList();
    void rebuildBrowseList();
    void refreshGenreFilter();
    void refreshTagPicker();
    void moveSelection(int delta);
    void cycleTab(int delta);
    bool handleNavKey(QKeyEvent* event);
    void activateCurrent();
    void removeCurrent();
    void favoriteCurrent();
    void submitAdd();
    void runBrowseQuery();
    void restyle();
    void updateHint();

    StationLibrary* library_ = nullptr;
    RadioDirectory* directory_ = nullptr;

    QWidget* card_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* status_ = nullptr;
    QStackedWidget* pages_ = nullptr;

    // Library page.
    QLineEdit* search_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QComboBox* genre_ = nullptr;
    QListWidget* list_ = nullptr;
    QCheckBox* mini_ = nullptr;

    // Add page.
    QLineEdit* addName_ = nullptr;
    QLineEdit* addUrl_ = nullptr;
    QListWidget* addTags_ = nullptr;

    Theme theme_ = Theme::builtin();
    Mode mode_ = Mode::Library;
    // Rows currently rendered, parallel to list_, so a selection maps back to
    // real data without stuffing structs into item roles.
    QVector<RadioStation> shown_;
    QVector<DirectoryStation> browseResults_;
    QString playingId_;
};
