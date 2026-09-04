#pragma once

#include <QChar>
#include <QPair>
#include <QString>
#include <QVector>
#include <cstdint>

enum : int {
    StateNone = 0,
    StateFence = 1,
    StateTable = 2,
};

enum class SpanKind : std::uint8_t {
    HiddenMarker,
    Marker,
    HeadingText,
    Bold,
    Italic,
    Strike,
    Code,
    LinkText,
    AnchorLinkText,
    LinkUrl,
    Quote,
    ListMarker,
    Checkbox,
    Rule,
    TablePipe,
    TableHeaderText,
    TableCellText,
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
    TableHeader,
    TableDelimiter,
    TableRow,
    TocOpen,
    TocClose,
};

struct Span {
    int start = 0;
    int length = 0;
    SpanKind kind = SpanKind::Marker;
    int headingLevel = 0;
};

struct LinkRef {
    int start = 0;
    int length = 0;
    QString target;
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
    QVector<LinkRef> links;
    QVector<QPair<int, int>> tableCells;
    QVector<int> tablePipes;
};

class MarkdownRules {
public:
    static ParseResult parseLine(const QString& text, int fenceState, bool revealed);
    static bool isFence(const QString& text);
    static QString fenceLanguage(const QString& text);
};
