#pragma once

#include <QObject>
#include <QString>
#include <QTextDocument>
#include <QUuid>
#include <QVector>

class Buffer : public QObject {
    Q_OBJECT

public:
    explicit Buffer(QObject* parent = nullptr);
    Buffer(const QString& id, QObject* parent = nullptr);

    QString id() const { return id_; }
    QString title() const { return title_; }
    QString path() const { return path_; }
    bool isDirty() const { return dirty_; }
    bool isUnnamed() const { return path_.isEmpty(); }
    int cursor() const { return cursor_; }
    int scroll() const { return scroll_; }
    QTextDocument* document() const { return document_; }
    QString text() const;

    void setTitle(const QString& title);
    void setPath(const QString& path);
    void setCursor(int pos);
    void setScroll(int value);
    void setText(const QString& text, bool markClean = true);
    void markClean();
    void markDirty();

    void captureHistory(int cursor);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    bool isRestoring() const { return restoring_; }

signals:
    void titleChanged();
    void dirtyChanged();
    void contentsChanged();

private:
    struct HistEntry {
        QString text;
        int cursor = 0;
    };

    void resetHistory();
    void applyHistory();

    QString id_;
    QString title_ = QStringLiteral("scratch");
    QString path_;
    int cursor_ = 0;
    int scroll_ = 0;
    bool dirty_ = false;
    bool restoring_ = false;
    QTextDocument* document_ = nullptr;
    QVector<HistEntry> history_;
    int historyIndex_ = 0;
};
