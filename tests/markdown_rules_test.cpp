#include "markdown/CodeSyntax.h"
#include "markdown/MarkdownRules.h"

#include <gtest/gtest.h>

TEST(MarkdownRules, HeadingHiddenAndRevealed) {
    const ParseResult hidden = MarkdownRules::parseLine(QStringLiteral("# hello"), 0, false);
    EXPECT_EQ(hidden.headingLevel, 1);
    EXPECT_EQ(hidden.kind, BlockKind::Heading);
    ASSERT_FALSE(hidden.spans.isEmpty());
    EXPECT_EQ(hidden.spans.front().kind, SpanKind::HiddenMarker);
    bool sawHeading = false;
    for (const Span& span : hidden.spans) {
        if (span.kind == SpanKind::HeadingText) {
            sawHeading = true;
            EXPECT_EQ(QStringLiteral("# hello").mid(span.start, span.length), QStringLiteral("hello"));
        }
    }
    EXPECT_TRUE(sawHeading);

    const ParseResult shown = MarkdownRules::parseLine(QStringLiteral("# hello"), 0, true);
    EXPECT_EQ(shown.spans.front().kind, SpanKind::Marker);
}

TEST(MarkdownRules, BoldAndItalic) {
    const ParseResult r = MarkdownRules::parseLine(QStringLiteral("**bold** and *em*"), 0, false);
    int bold = 0;
    int italic = 0;
    for (const Span& span : r.spans) {
        if (span.kind == SpanKind::Bold) {
            ++bold;
            EXPECT_EQ(QStringLiteral("**bold** and *em*").mid(span.start, span.length), QStringLiteral("bold"));
        }
        if (span.kind == SpanKind::Italic) {
            ++italic;
        }
    }
    EXPECT_EQ(bold, 1);
    EXPECT_EQ(italic, 1);
}

TEST(MarkdownRules, CodeSpan) {
    const ParseResult r = MarkdownRules::parseLine(QStringLiteral("use `code` here"), 0, true);
    bool saw = false;
    for (const Span& span : r.spans) {
        if (span.kind == SpanKind::Code) {
            saw = true;
            EXPECT_EQ(QStringLiteral("use `code` here").mid(span.start, span.length), QStringLiteral("code"));
        }
    }
    EXPECT_TRUE(saw);
}

TEST(MarkdownRules, FenceLanguage) {
    EXPECT_EQ(MarkdownRules::fenceLanguage(QStringLiteral("```python")), QStringLiteral("python"));
    EXPECT_EQ(MarkdownRules::fenceLanguage(QStringLiteral("```js extra")), QStringLiteral("js"));
    EXPECT_EQ(MarkdownRules::fenceLanguage(QStringLiteral("~~~cpp")), QStringLiteral("cpp"));
    EXPECT_TRUE(MarkdownRules::fenceLanguage(QStringLiteral("```")).isEmpty());
    const ParseResult tagged = MarkdownRules::parseLine(QStringLiteral("```python"), 0, false);
    EXPECT_EQ(tagged.kind, BlockKind::FenceOpen);
    EXPECT_EQ(tagged.fenceLang, QStringLiteral("python"));
}

TEST(MarkdownRules, FenceState) {
    EXPECT_TRUE(MarkdownRules::isFence(QStringLiteral("```cpp")));
    const ParseResult open = MarkdownRules::parseLine(QStringLiteral("```"), 0, false);
    EXPECT_EQ(open.kind, BlockKind::FenceOpen);
    EXPECT_EQ(open.nextFenceState, 1);
    const ParseResult body = MarkdownRules::parseLine(QStringLiteral("int x;"), 1, false);
    EXPECT_EQ(body.kind, BlockKind::FenceBody);
    EXPECT_EQ(body.nextFenceState, 1);
    EXPECT_EQ(body.spans.front().kind, SpanKind::Code);
    const ParseResult close = MarkdownRules::parseLine(QStringLiteral("```"), 1, false);
    EXPECT_EQ(close.kind, BlockKind::FenceClose);
    EXPECT_EQ(close.nextFenceState, 0);
}

TEST(MarkdownRules, SameLineFence) {
    const ParseResult a = MarkdownRules::parseLine(QStringLiteral("```hello```"), 0, false);
    EXPECT_EQ(a.kind, BlockKind::FenceSingle);
    EXPECT_EQ(a.nextFenceState, 0);
    EXPECT_EQ(a.fenceLine, 1);
    bool sawCode = false;
    for (const Span& span : a.spans) {
        if (span.kind == SpanKind::Code) {
            sawCode = true;
            EXPECT_EQ(QStringLiteral("```hello```").mid(span.start, span.length), QStringLiteral("hello"));
        }
    }
    EXPECT_TRUE(sawCode);

    const ParseResult b = MarkdownRules::parseLine(QStringLiteral("``` hello ```"), 0, true);
    EXPECT_EQ(b.kind, BlockKind::FenceSingle);
    EXPECT_EQ(b.nextFenceState, 0);
    for (const Span& span : b.spans) {
        if (span.kind == SpanKind::Code) {
            EXPECT_EQ(QStringLiteral("``` hello ```").mid(span.start, span.length), QStringLiteral(" hello "));
        }
    }
}

TEST(MarkdownRules, ListAndLink) {
    const ParseResult list = MarkdownRules::parseLine(QStringLiteral("- [x] task"), 0, true);
    EXPECT_EQ(list.kind, BlockKind::List);
    bool checkbox = false;
    for (const Span& span : list.spans) {
        if (span.kind == SpanKind::Checkbox) {
            checkbox = true;
        }
    }
    EXPECT_TRUE(checkbox);

    const ParseResult link = MarkdownRules::parseLine(QStringLiteral("see [docs](https://x.test)"), 0, true);
    bool text = false;
    bool url = false;
    for (const Span& span : link.spans) {
        if (span.kind == SpanKind::LinkText) {
            text = true;
        }
        if (span.kind == SpanKind::LinkUrl) {
            url = true;
        }
    }
    EXPECT_TRUE(text);
    EXPECT_TRUE(url);
}

TEST(MarkdownRules, UnorderedAndOrderedLists) {
    const ParseResult star = MarkdownRules::parseLine(QStringLiteral("* Item 1"), 0, false);
    EXPECT_EQ(star.kind, BlockKind::List);
    EXPECT_EQ(star.listMarker, QLatin1Char('*'));
    ASSERT_FALSE(star.spans.isEmpty());
    EXPECT_EQ(star.spans.front().kind, SpanKind::HiddenMarker);

    const ParseResult dash = MarkdownRules::parseLine(QStringLiteral("- Item 2"), 0, false);
    EXPECT_EQ(dash.kind, BlockKind::List);
    EXPECT_EQ(dash.listMarker, QLatin1Char('-'));

    const ParseResult plus = MarkdownRules::parseLine(QStringLiteral("+ Item"), 0, true);
    EXPECT_EQ(plus.kind, BlockKind::List);
    EXPECT_EQ(plus.spans.front().kind, SpanKind::ListMarker);

    const ParseResult ordered = MarkdownRules::parseLine(QStringLiteral("1. Item"), 0, false);
    EXPECT_EQ(ordered.kind, BlockKind::OrderedList);
    EXPECT_EQ(ordered.spans.front().kind, SpanKind::ListMarker);

    const ParseResult notList = MarkdownRules::parseLine(QStringLiteral("*Item"), 0, false);
    EXPECT_NE(notList.kind, BlockKind::List);
}

TEST(MarkdownRules, NestedLists) {
    const ParseResult nested = MarkdownRules::parseLine(QStringLiteral("  - child"), 0, false);
    EXPECT_EQ(nested.kind, BlockKind::List);
    EXPECT_EQ(nested.listLevel, 1);

    const ParseResult deeper = MarkdownRules::parseLine(QStringLiteral("    * grandchild"), 0, false);
    EXPECT_EQ(deeper.kind, BlockKind::List);
    EXPECT_EQ(deeper.listLevel, 2);

    const ParseResult stars = MarkdownRules::parseLine(QStringLiteral("** sub"), 0, false);
    EXPECT_EQ(stars.kind, BlockKind::List);
    EXPECT_EQ(stars.listLevel, 1);

    const ParseResult stars3 = MarkdownRules::parseLine(QStringLiteral("*** deep"), 0, false);
    EXPECT_EQ(stars3.kind, BlockKind::List);
    EXPECT_EQ(stars3.listLevel, 2);
}

TEST(MarkdownRules, HorizontalRule) {
    const ParseResult hidden = MarkdownRules::parseLine(QStringLiteral("---"), 0, false);
    EXPECT_EQ(hidden.kind, BlockKind::Rule);
    ASSERT_FALSE(hidden.spans.isEmpty());
    EXPECT_EQ(hidden.spans.front().kind, SpanKind::HiddenMarker);

    const ParseResult shown = MarkdownRules::parseLine(QStringLiteral("***"), 0, true);
    EXPECT_EQ(shown.kind, BlockKind::Rule);
    EXPECT_EQ(shown.spans.front().kind, SpanKind::Rule);
}

TEST(MarkdownRules, BlockImage) {
    const ParseResult img = MarkdownRules::parseLine(QStringLiteral("![](shot.png)"), 0, false);
    EXPECT_EQ(img.kind, BlockKind::Image);
    EXPECT_EQ(img.imagePath, QStringLiteral("shot.png"));
    ASSERT_FALSE(img.spans.isEmpty());
    EXPECT_EQ(img.spans.front().kind, SpanKind::HiddenMarker);

    const ParseResult titled =
        MarkdownRules::parseLine(QStringLiteral("![alt](./pic.jpg \"cap\")"), 0, true);
    EXPECT_EQ(titled.kind, BlockKind::Image);
    EXPECT_EQ(titled.imagePath, QStringLiteral("./pic.jpg"));

    const ParseResult notOnly =
        MarkdownRules::parseLine(QStringLiteral("see ![x](y.png) please"), 0, false);
    EXPECT_NE(notOnly.kind, BlockKind::Image);
}

TEST(CodeSyntax, PythonTokens) {
    EXPECT_EQ(CodeSyntax::normalizeLang(QStringLiteral("py")), QStringLiteral("python"));
    const QString line = QStringLiteral("def foo():  # hi");
    const auto tokens = CodeSyntax::tokenize(line, QStringLiteral("python"));
    bool sawDef = false;
    bool sawComment = false;
    for (const CodeToken& tok : tokens) {
        if (tok.kind == CodeTokenKind::Keyword
            && line.mid(tok.start, tok.length) == QStringLiteral("def")) {
            sawDef = true;
        }
        if (tok.kind == CodeTokenKind::Comment) {
            sawComment = true;
        }
    }
    EXPECT_TRUE(sawDef);
    EXPECT_TRUE(sawComment);
    EXPECT_TRUE(CodeSyntax::tokenize(QStringLiteral("x = 1"), QStringLiteral("unknown")).isEmpty());
}
