#pragma once

#include "theme/Theme.h"

#include <QWidget>

class BufferManager;
class QLineEdit;
class QListWidget;

class TabSwitcher : public QWidget {
    Q_OBJECT

public:
    explicit TabSwitcher(BufferManager* buffers, QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void open();

signals:
    void chosen(int index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuild();
    void acceptCurrent();

    BufferManager* buffers_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    Theme theme_ = Theme::builtin();
};
