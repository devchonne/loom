#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct OutlineEntry {
    int blockNumber = 0;
    int level = 0;
    QString text;
    QString slug;
};

class DocumentOutline {
public:
    static QVector<OutlineEntry> build(const QStringList& lines);
    static QString slugify(const QString& headingText);
    static QString tocMarkdown(const QVector<OutlineEntry>& entries, int maxLevel = 6);
    static QString stripMarkup(const QString& text);
};
