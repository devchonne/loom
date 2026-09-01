#pragma once

#include <QChar>
#include <QString>
#include <QVector>
#include <cstdint>

enum class SpanKind : std::uint8_t {
    HiddenMarker,
    Marker,
    HeadingText,
    Bold,
    Italic,
    Strike,
    Code,
    LinkText,
    LinkUrl,
    Quote,
    ListMarker,
    Checkbox,
    Rule,
};

enum class BlockKind : std::uint8_t {
    Paragraph,
    Heading,
    List,
    OrderedList,
    Quote,
    Rule,
    FenceOpen,
    FenceBody,
    FenceClose,
    FenceSingle,
    Image,
};

struct Span {
    int start = 0;
    int length = 0;
    SpanKind kind = SpanKind::Marker;
    int headingLevel = 0;
};

struct ParseResult {
    QVector<Span> spans;
    BlockKind kind = BlockKind::Paragraph;
    int nextFenceState = 0;
    int headingLevel = 0;
    int fenceLine = 0;
    int markerStart = 0;
    int markerLength = 0;
    int listLevel = 0;
    QChar listMarker;
    QString fenceLang;
    QString imagePath;
};

class MarkdownRules {
public:
    static ParseResult parseLine(const QString& text, int fenceState, bool revealed);
    static bool isFence(const QString& text);
    static QString fenceLanguage(const QString& text);
};
