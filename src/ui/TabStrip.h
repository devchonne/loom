#pragma once

#include "theme/Theme.h"

#include <QWidget>

class BufferManager;

class TabStrip : public QWidget {
    Q_OBJECT

public:
    explicit TabStrip(BufferManager* buffers, QWidget* parent = nullptr);

    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void setArmedIndex(int index);
    void setComparePair(int left, int right);

signals:
    void tabClicked(int index);
    void tabShiftClicked(int index);
    void tabCloseRequested(int index);
    void tabMoved(int from, int to);
    void tabRenameRequested(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    int tabAt(const QPoint& pos) const;
    QRect tabRect(int index) const;
    int tabWidth() const;

    BufferManager* buffers_ = nullptr;
    Theme theme_ = Theme::builtin();
    int dragFrom_ = -1;
    int hover_ = -1;
    int armed_ = -1;
    int compareLeft_ = -1;
    int compareRight_ = -1;
};
