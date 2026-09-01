#pragma once

#include "theme/Theme.h"
#include "ui/Weather.h"

#include <QColor>
#include <QEvent>
#include <QHash>
#include <QPair>
#include <QPixmap>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QVector>

class Buffer;
class MarkdownHighlighter;
class RevealController;
class QKeyEvent;
class QMimeData;
struct Settings;

class Editor : public QTextEdit {
    Q_OBJECT

public:
    explicit Editor(QWidget* parent = nullptr);

    void applySettings(const Settings& settings);
    void setTheme(const Theme& theme);
    void bindDocument(QTextDocument* document, bool fresh);
    void unbindDocument();
    void setZoom(int percent);
    int zoom() const { return zoom_; }
    void zoomBy(int delta);
    void resetZoom();
    void setZen(bool zen);
    void setMarkdownEnabled(bool enabled);
    bool markdownEnabled() const;
    void applyLineHeight();
    int wordCount() const;
    MarkdownHighlighter* highlighter() const { return highlighter_; }
    void setWeatherMode(WeatherMode mode, bool toggleIfSame = true);
    WeatherMode weatherMode() const { return weather_.mode(); }
    void setResourceDir(const QString& dir);
    void setDiffHighlights(const QVector<QPair<int, QColor>>& tints);
    void undoEdit();
    void redoEdit();

signals:
    void zoomChanged(int percent);
    void cursorInfoChanged();
    void slashCommand(const QString& name, const QString& arg, bool* accepted);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    void drawScanlines(QPainter& painter);
    void drawVignette(QPainter& painter);
    void drawCaret(QPainter& painter);
    void drawMarkdownChrome(QPainter& viewportPainter);
    void drawFenceLineNumbers(QPainter& viewportPainter);
    void syncListMargins();
    void applyHistoryCursor();
    Buffer* boundBuffer() const;
    bool isUndoRedoKey(const QKeyEvent* event) const;
    void updateZenMargins();
    void restartCaret();
    void syncViewportFill();
    void syncWeatherTimer();
    bool trySlashCommand();
    void consumeCurrentLine();
    void addCaretVertical(int delta);
    void clearMultiCarets();
    QVector<int> allCaretPositions() const;
    void commitCarets(QVector<int> positions, int primary);
    void insertAtCarets(const QString& text);
    void deleteAtCarets(bool forward);
    void moveCarets(QTextCursor::MoveOperation op);
    void drawOneCaret(QPainter& painter, const QTextCursor& cursor);
    void applyBodyCharFormat(QTextCursor& cursor);
    bool tryInsertImage(const QMimeData* source);
    void insertImageMarkdown(const QString& filename);
    QString savePastedImage(const QImage& image) const;
    QString resolveImagePath(const QString& spec) const;
    QPixmap cachedPixmap(const QString& spec) const;
    QSize imageDisplaySize(const QString& spec) const;
    bool isImageBlock(const QTextBlock& block) const;
    void removeImageLine();

    MarkdownHighlighter* highlighter_ = nullptr;
    RevealController* reveal_ = nullptr;
    Theme theme_ = Theme::builtin();
    Weather weather_;
    int zoom_ = 100;
    qreal basePointSize_ = 13.5;
    double lineHeight_ = 1.5;
    bool zen_ = false;
    bool blockCaret_ = true;
    double scanlineIntensity_ = 0.04;
    bool caretVisible_ = true;
    QTimer* caretTimer_ = nullptr;
    QTimer* weatherTimer_ = nullptr;
    QPixmap scanlineTile_;
    QVector<int> extraCarets_;
    int multiColumn_ = -1;
    QString resourceDir_;
    mutable QHash<QString, QPixmap> imageCache_;
};
