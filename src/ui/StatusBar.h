#pragma once

#include "theme/Theme.h"

#include <QWidget>

class StatusBar : public QWidget {
    Q_OBJECT

public:
    explicit StatusBar(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void setInfo(const QString& title, int words, int line, int column, bool dirty, int zoom,
                 const QString& themeName = {});

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    Theme theme_ = Theme::builtin();
    QString text_;
    QString themeName_;
};
