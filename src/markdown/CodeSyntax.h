#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

enum class CodeTokenKind : std::uint8_t { Keyword, Builtin, String, Comment, Number };

struct CodeToken {
    int start = 0;
    int length = 0;
    CodeTokenKind kind = CodeTokenKind::Keyword;
};

namespace CodeSyntax {
QString normalizeLang(const QString& raw);
QVector<CodeToken> tokenize(const QString& text, const QString& lang);
}
