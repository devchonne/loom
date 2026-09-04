#pragma once

#include "markdown/DocumentOutline.h"
#include "theme/Theme.h"

#include <QVector>
#include <QWidget>

class QLineEdit;
class QListWidget;

class OutlineOverlay : public QWidget {
    Q_OBJECT

public:
    explicit OutlineOverlay(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void open(const QVector<OutlineEntry>& entries);

signals:
    void chosen(int blockNumber);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuild();
    void acceptCurrent();

    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    Theme theme_ = Theme::builtin();
    QVector<OutlineEntry> entries_;
};
