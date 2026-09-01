#pragma once

#include <QString>

class MarkdownExporter {
public:
    static QString toHtml(const QString& markdown);
};
