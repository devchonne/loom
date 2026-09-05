#pragma once

#include "theme/Theme.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;

// Hidden overlay: the only way in is Ctrl+Shift+P or /pdf.
class PdfExportOverlay : public QWidget {
    Q_OBJECT

public:
    explicit PdfExportOverlay(QWidget* parent = nullptr);
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
    void restyle();

    QLabel* title_ = nullptr;
    QLabel* hint_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
    Theme theme_ = Theme::builtin();
    QString currentId_;
};
