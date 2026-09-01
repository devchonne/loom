#pragma once

#include "theme/Theme.h"

#include <QWidget>

class QLineEdit;

class FindBar : public QWidget {
    Q_OBJECT

public:
    explicit FindBar(QWidget* parent = nullptr);
    void setTheme(const Theme& theme);
    void setChromeFont(const QFont& font);
    void open(const QString& seed = {});
    QString query() const;

signals:
    void findNext();
    void findPrev();
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* input_ = nullptr;
};
