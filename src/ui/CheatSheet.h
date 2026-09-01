#pragma once

#include "theme/Theme.h"

#include <QWidget>

class CheatSheet : public QWidget {
    Q_OBJECT

public:
    explicit CheatSheet(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void toggle();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    Theme theme_ = Theme::builtin();
};
