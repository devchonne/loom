#pragma once

#include "theme/Theme.h"

#include <QWidget>

class QLineEdit;
class QListWidget;

class ThemeSwitcher : public QWidget {
    Q_OBJECT

public:
    explicit ThemeSwitcher(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void open(const QString& currentId);

signals:
    void chosen(const QString& id);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuild();
    void acceptCurrent();

    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    Theme theme_ = Theme::builtin();
    QString currentId_;
};
