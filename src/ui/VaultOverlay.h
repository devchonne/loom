#pragma once

#include "core/Vault.h"
#include "theme/Theme.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;

// One overlay serving the vault's three list-shaped needs: pick a note, pick a
// backlink, pick a vault. Same card, same keys, same shape as OutlineOverlay —
// the mode only changes the placeholder and what a row carries.
class VaultOverlay : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        Notes,     // quick switcher across the vault
        Backlinks, // notes referring to the current one
        Vaults,    // switch between known vaults
    };

    explicit VaultOverlay(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);

    void openNotes(const QVector<VaultNote>& notes);
    void openBacklinks(const QVector<VaultBacklink>& links, const QString& title);
    // Rows are (label, root); the active root is preselected.
    void openVaults(const QVector<QPair<QString, QString>>& vaults, const QString& activeRoot);

signals:
    // A note or backlink was chosen. line is 0 when there is no specific line.
    void pathChosen(const QString& path, int line);
    void vaultChosen(const QString& root);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct Row {
        QString label;
        QString detail;
        QString path;
        int line = 0;
    };

    void present(const QString& title, const QString& placeholder);
    void rebuild();
    void acceptCurrent();

    QLabel* title_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    Theme theme_ = Theme::builtin();
    QVector<Row> rows_;
    Mode mode_ = Mode::Notes;
};
