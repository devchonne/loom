#pragma once

#include "theme/Theme.h"

#include <QWidget>

class CrtWipe : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit CrtWipe(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void play();
    qreal progress() const { return progress_; }
    void setProgress(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Theme theme_ = Theme::builtin();
    qreal progress_ = 0;
};
