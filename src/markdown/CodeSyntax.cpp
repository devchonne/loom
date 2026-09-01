#include "markdown/CodeSyntax.h"

#include <QHash>
#include <QSet>

#include <initializer_list>

namespace {
enum class CommentStyle : std::uint8_t { None, Hash, Slash, Dash };

struct LangSpec {
    QSet<QString> keywords;
    QSet<QString> builtins;
    CommentStyle comments = CommentStyle::None;
    bool ticks = false;
};

QSet<QString> words(std::initializer_list<const char*> list) {
    QSet<QString> set;
    for (const char* w : list) {
        set.insert(QString::fromLatin1(w));
    }
    return set;
}

const LangSpec& specFor(const QString& lang) {
    static const QHash<QString, LangSpec> kSpecs = {
        {QStringLiteral("python"),
         {words({"and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
                 "elif", "else", "except", "False", "finally", "for", "from", "global", "if", "import",
                 "in", "is", "lambda", "None", "nonlocal", "not", "or", "pass", "raise", "return",
                 "True", "try", "while", "with", "yield"}),
          words({"print", "len", "range", "str", "int", "list", "dict", "set", "open", "type", "self",
                 "super", "None"}),
          CommentStyle::Hash, false}},
        {QStringLiteral("javascript"),
         {words({"async", "await", "break", "case", "catch", "class", "const", "continue", "debugger",
                 "default", "delete", "do", "else", "export", "extends", "false", "finally", "for",
                 "function", "if", "import", "in", "instanceof", "let", "new", "null", "return",
                 "static", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void",
                 "while", "with", "yield"}),
          words({"console", "document", "window", "undefined", "NaN", "Infinity"}),
          CommentStyle::Slash, true}},
        {QStringLiteral("c"),
         {words({"auto", "break", "case", "char", "const", "continue", "default", "do", "double",
                 "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
                 "register", "return", "short", "signed", "sizeof", "static", "struct", "switch",
                 "typedef", "union", "unsigned", "void", "volatile", "while"}),
          words({"NULL", "true", "false"}), CommentStyle::Slash, false}},
        {QStringLiteral("cpp"),
         {words({"alignas", "alignof", "and", "auto", "bool", "break", "case", "catch", "char",
                 "class", "const", "consteval", "constexpr", "continue", "decltype", "default",
                 "delete", "do", "double", "else", "enum", "explicit", "export", "extern", "false",
                 "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
                 "namespace", "new", "noexcept", "not", "nullptr", "operator", "or", "private",
                 "protected", "public", "return", "short", "signed", "sizeof", "static", "struct",
                 "switch", "template", "this", "throw", "true", "try", "typedef", "typename", "union",
                 "unsigned", "using", "virtual", "void", "volatile", "while"}),
          words({"std", "cout", "cin", "endl", "string", "vector"}), CommentStyle::Slash, false}},
        {QStringLiteral("rust"),
         {words({"as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum",
                 "extern", "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod",
                 "move", "mut", "pub", "ref", "return", "self", "Self", "static", "struct", "super",
                 "trait", "true", "type", "unsafe", "use", "where", "while"}),
          words({"Some", "None", "Ok", "Err", "vec", "String"}), CommentStyle::Slash, false}},
        {QStringLiteral("go"),
         {words({"break", "case", "chan", "const", "continue", "default", "defer", "else", "fallthrough",
                 "for", "func", "go", "goto", "if", "import", "interface", "map", "package", "range",
                 "return", "select", "struct", "switch", "type", "var"}),
          words({"nil", "true", "false", "iota", "make", "len", "append"}), CommentStyle::Slash, false}},
        {QStringLiteral("bash"),
         {words({"alias", "break", "case", "continue", "do", "done", "elif", "else", "esac", "export",
                 "fi", "for", "function", "if", "in", "local", "return", "select", "shift", "then",
                 "time", "until", "while"}),
          words({"echo", "cd", "exit", "export", "source"}), CommentStyle::Hash, false}},
        {QStringLiteral("json"), {words({}), words({"true", "false", "null"}), CommentStyle::None, false}},
        {QStringLiteral("ruby"),
         {words({"alias", "and", "begin", "break", "case", "class", "def", "do", "else", "elsif",
                 "end", "ensure", "false", "for", "if", "in", "module", "next", "nil", "not", "or",
                 "redo", "rescue", "retry", "return", "self", "super", "then", "true", "undef",
                 "unless", "until", "when", "while", "yield"}),
          words({"puts", "require", "attr_accessor"}), CommentStyle::Hash, false}},
        {QStringLiteral("java"),
         {words({"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class",
                 "const", "continue", "default", "do", "double", "else", "enum", "extends", "false",
                 "final", "finally", "float", "for", "goto", "if", "implements", "import", "instanceof",
                 "int", "interface", "long", "native", "new", "null", "package", "private", "protected",
                 "public", "return", "short", "static", "strictfp", "super", "switch", "synchronized",
                 "this", "throw", "throws", "transient", "true", "try", "void", "volatile", "while"}),
          words({"String", "System", "Object"}), CommentStyle::Slash, false}},
        {QStringLiteral("lua"),
         {words({"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
                 "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until",
                 "while"}),
          words({"print", "pairs", "ipairs", "require"}), CommentStyle::Dash, false}},
        {QStringLiteral("sql"),
         {words({"ADD", "ALL", "ALTER", "AND", "AS", "ASC", "BETWEEN", "BY", "CASE", "CREATE",
                 "DELETE", "DESC", "DISTINCT", "DROP", "ELSE", "END", "EXISTS", "FROM", "GROUP",
                 "HAVING", "IN", "INSERT", "INTO", "IS", "JOIN", "KEY", "LEFT", "LIKE", "LIMIT",
                 "NOT", "NULL", "ON", "OR", "ORDER", "PRIMARY", "RIGHT", "SELECT", "SET", "TABLE",
                 "THEN", "UNION", "UPDATE", "VALUES", "WHEN", "WHERE"}),
          words({}), CommentStyle::Dash, false}},
        {QStringLiteral("toml"),
         {words({"true", "false"}), words({}), CommentStyle::Hash, false}},
        {QStringLiteral("yaml"),
         {words({"true", "false", "null", "yes", "no"}), words({}), CommentStyle::Hash, false}},
    };
    static const LangSpec kEmpty;
    const auto it = kSpecs.find(lang);
    return it == kSpecs.end() ? kEmpty : it.value();
}

bool isIdentStart(QChar c) {
    return c.isLetter() || c == QLatin1Char('_');
}

bool isIdent(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}
}

QString CodeSyntax::normalizeLang(const QString& raw) {
    const QString id = raw.trimmed().toLower();
    if (id == QLatin1String("py") || id == QLatin1String("python3")) {
        return QStringLiteral("python");
    }
    if (id == QLatin1String("js") || id == QLatin1String("node") || id == QLatin1String("javascript")) {
        return QStringLiteral("javascript");
    }
    if (id == QLatin1String("ts") || id == QLatin1String("typescript")) {
        return QStringLiteral("javascript");
    }
    if (id == QLatin1String("c++") || id == QLatin1String("cxx") || id == QLatin1String("cc")
        || id == QLatin1String("hpp") || id == QLatin1String("h++")) {
        return QStringLiteral("cpp");
    }
    if (id == QLatin1String("h")) {
        return QStringLiteral("c");
    }
    if (id == QLatin1String("rs")) {
        return QStringLiteral("rust");
    }
    if (id == QLatin1String("sh") || id == QLatin1String("zsh") || id == QLatin1String("shell")) {
        return QStringLiteral("bash");
    }
    if (id == QLatin1String("yml")) {
        return QStringLiteral("yaml");
    }
    return id;
}

QVector<CodeToken> CodeSyntax::tokenize(const QString& text, const QString& lang) {
    const QString id = normalizeLang(lang);
    const LangSpec& spec = specFor(id);
    if (spec.keywords.isEmpty() && spec.builtins.isEmpty() && spec.comments == CommentStyle::None) {
        if (id != QLatin1String("json")) {
            return {};
        }
    }

    QVector<CodeToken> tokens;
    const int n = text.size();
    int i = 0;
    auto push = [&](int start, int length, CodeTokenKind kind) {
        if (length > 0) {
            tokens.push_back(CodeToken{start, length, kind});
        }
    };

    while (i < n) {
        const QChar c = text[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }

        if (spec.comments == CommentStyle::Hash && c == QLatin1Char('#')) {
            push(i, n - i, CodeTokenKind::Comment);
            break;
        }
        if (spec.comments == CommentStyle::Dash && c == QLatin1Char('-') && i + 1 < n
            && text[i + 1] == QLatin1Char('-')) {
            push(i, n - i, CodeTokenKind::Comment);
            break;
        }
        if (spec.comments == CommentStyle::Slash && c == QLatin1Char('/') && i + 1 < n) {
            if (text[i + 1] == QLatin1Char('/')) {
                push(i, n - i, CodeTokenKind::Comment);
                break;
            }
            if (text[i + 1] == QLatin1Char('*')) {
                const int close = text.indexOf(QStringLiteral("*/"), i + 2);
                const int end = close < 0 ? n : close + 2;
                push(i, end - i, CodeTokenKind::Comment);
                i = end;
                continue;
            }
        }

        if (c == QLatin1Char('"') || c == QLatin1Char('\'')
            || (spec.ticks && c == QLatin1Char('`'))) {
            const QChar q = c;
            int j = i + 1;
            while (j < n) {
                if (text[j] == QLatin1Char('\\') && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if (text[j] == q) {
                    ++j;
                    break;
                }
                ++j;
            }
            push(i, j - i, CodeTokenKind::String);
            i = j;
            continue;
        }

        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && text[i + 1].isDigit())) {
            int j = i + 1;
            while (j < n && (text[j].isDigit() || text[j] == QLatin1Char('.') || text[j] == QLatin1Char('_')
                             || text[j].toLower() == QLatin1Char('x') || text[j].toLower() == QLatin1Char('b')
                             || (text[j].toLower() >= QLatin1Char('a') && text[j].toLower() <= QLatin1Char('f')))) {
                ++j;
            }
            push(i, j - i, CodeTokenKind::Number);
            i = j;
            continue;
        }

        if (isIdentStart(c)) {
            int j = i + 1;
            while (j < n && isIdent(text[j])) {
                ++j;
            }
            const QString word = text.mid(i, j - i);
            if (spec.keywords.contains(word)
                || (spec.comments == CommentStyle::Dash && spec.keywords.contains(word.toUpper()))) {
                push(i, j - i, CodeTokenKind::Keyword);
            } else if (spec.builtins.contains(word)) {
                push(i, j - i, CodeTokenKind::Builtin);
            }
            i = j;
            continue;
        }

        ++i;
    }
    return tokens;
}
