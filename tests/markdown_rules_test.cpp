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
    EXPECT_GE(list.checkboxStart, 0);
    EXPECT_TRUE(list.checkboxChecked);
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

TEST(MarkdownRules, CheckboxRevealedVsHidden) {
    const ParseResult hidden = MarkdownRules::parseLine(QStringLiteral("- [ ] task"), 0, false);
    EXPECT_EQ(hidden.kind, BlockKind::List);
    EXPECT_GE(hidden.checkboxStart, 0);
    EXPECT_FALSE(hidden.checkboxChecked);
    bool hiddenSpanFound = false;
    bool visibleCheckboxSpanFound = false;
    for (const Span& span : hidden.spans) {
        if (span.start == hidden.checkboxStart) {
            if (span.kind == SpanKind::HiddenMarker) {
                hiddenSpanFound = true;
            } else if (span.kind == SpanKind::Checkbox) {
                visibleCheckboxSpanFound = true;
            }
        }
    }
    EXPECT_TRUE(hiddenSpanFound);
    EXPECT_FALSE(visibleCheckboxSpanFound);

    const ParseResult revealed = MarkdownRules::parseLine(QStringLiteral("- [X] task"), 0, true);
    EXPECT_EQ(revealed.kind, BlockKind::List);
    EXPECT_GE(revealed.checkboxStart, 0);
    EXPECT_TRUE(revealed.checkboxChecked);
    bool checkboxSpanFound = false;
    for (const Span& span : revealed.spans) {
        if (span.start == revealed.checkboxStart && span.kind == SpanKind::Checkbox) {
            checkboxSpanFound = true;
        }
    }
    EXPECT_TRUE(checkboxSpanFound);
}

TEST(MarkdownRules, StandaloneChecklistBracketDepth) {
    const ParseResult top = MarkdownRules::parseLine(QStringLiteral("[] Wash dishes"), 0, false);
    EXPECT_EQ(top.kind, BlockKind::Checklist);
    EXPECT_EQ(top.listLevel, 0);
    EXPECT_FALSE(top.checkboxChecked);

    const ParseResult topChecked = MarkdownRules::parseLine(QStringLiteral("[x] Wash dishes"), 0, false);
    EXPECT_EQ(topChecked.kind, BlockKind::Checklist);
    EXPECT_EQ(topChecked.listLevel, 0);
    EXPECT_TRUE(topChecked.checkboxChecked);

    const ParseResult nested = MarkdownRules::parseLine(QStringLiteral("[[]] Sub task"), 0, false);
    EXPECT_EQ(nested.kind, BlockKind::Checklist);
    EXPECT_EQ(nested.listLevel, 1);
    EXPECT_FALSE(nested.checkboxChecked);

    const ParseResult nestedChecked = MarkdownRules::parseLine(QStringLiteral("[[x]] Sub task"), 0, false);
    EXPECT_EQ(nestedChecked.kind, BlockKind::Checklist);
    EXPECT_EQ(nestedChecked.listLevel, 1);
    EXPECT_TRUE(nestedChecked.checkboxChecked);

    // Mismatched bracket depth (open/close counts differ) is not a checklist.
    const ParseResult mismatched = MarkdownRules::parseLine(QStringLiteral("[[] Sub task"), 0, false);
    EXPECT_NE(mismatched.kind, BlockKind::Checklist);

    // Hidden (unrevealed) checklist marker collapses to a single hidden span;
    // revealed keeps it visible as SpanKind::Checkbox.
    bool hiddenSpanFound = false;
    for (const Span& span : nested.spans) {
        if (span.start == nested.checkboxStart && span.kind == SpanKind::HiddenMarker) {
            hiddenSpanFound = true;
        }
    }
    EXPECT_TRUE(hiddenSpanFound);

    const ParseResult revealedNested = MarkdownRules::parseLine(QStringLiteral("[[]] Sub task"), 0, true);
    bool checkboxSpanFound = false;
    for (const Span& span : revealedNested.spans) {
        if (span.start == revealedNested.checkboxStart && span.kind == SpanKind::Checkbox) {
            checkboxSpanFound = true;
        }
    }
    EXPECT_TRUE(checkboxSpanFound);
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

TEST(MarkdownRules, TableHeaderDelimiterAndRowKinds) {
    const ParseResult header = MarkdownRules::parseLine(QStringLiteral("| A | B |"), StateNone, false);
    EXPECT_EQ(header.kind, BlockKind::TableHeader);
    EXPECT_EQ(header.nextFenceState, StateTable);
    ASSERT_EQ(header.tableCells.size(), 2);

    const ParseResult delim =
        MarkdownRules::parseLine(QStringLiteral("| --- | --- |"), header.nextFenceState, false);
    EXPECT_EQ(delim.kind, BlockKind::TableDelimiter);
    EXPECT_EQ(delim.nextFenceState, StateTable);
    ASSERT_FALSE(delim.spans.isEmpty());
    EXPECT_EQ(delim.spans.front().kind, SpanKind::HiddenMarker);

    const ParseResult row =
        MarkdownRules::parseLine(QStringLiteral("| 1 | 2 |"), delim.nextFenceState, false);
    EXPECT_EQ(row.kind, BlockKind::TableRow);
    EXPECT_EQ(row.nextFenceState, StateTable);
    ASSERT_EQ(row.tableCells.size(), 2);
}

TEST(MarkdownRules, TableCellSpansAndKinds) {
    const ParseResult header = MarkdownRules::parseLine(QStringLiteral("| Name | Age |"), StateNone, false);
    EXPECT_EQ(header.kind, BlockKind::TableHeader);
    int pipes = 0;
    int headerText = 0;
    for (const Span& span : header.spans) {
        if (span.kind == SpanKind::TablePipe) {
            ++pipes;
        }
        if (span.kind == SpanKind::TableHeaderText) {
            ++headerText;
        }
    }
    EXPECT_EQ(pipes, 3);
    EXPECT_EQ(headerText, 2);

    const ParseResult row = MarkdownRules::parseLine(QStringLiteral("| a | b |"), StateTable, false);
    EXPECT_EQ(row.kind, BlockKind::TableRow);
    int cellText = 0;
    for (const Span& span : row.spans) {
        if (span.kind == SpanKind::TableCellText) {
            ++cellText;
        }
    }
    EXPECT_EQ(cellText, 2);
}

TEST(MarkdownRules, TableEscapedPipeDoesNotSplitCell) {
    const ParseResult row =
        MarkdownRules::parseLine(QStringLiteral("| a\\|b | c |"), StateNone, false);
    EXPECT_EQ(row.kind, BlockKind::TableHeader);
    ASSERT_EQ(row.tableCells.size(), 2);
    const QString text = QStringLiteral("| a\\|b | c |");
    EXPECT_EQ(text.mid(row.tableCells[0].first, row.tableCells[0].second).trimmed(),
              QStringLiteral("a\\|b"));
}

TEST(MarkdownRules, PipeInsideFenceStaysFenceBody) {
    const ParseResult body =
        MarkdownRules::parseLine(QStringLiteral("| not | a | table |"), StateFence, false);
    EXPECT_EQ(body.kind, BlockKind::FenceBody);
    EXPECT_EQ(body.nextFenceState, StateFence);
    ASSERT_FALSE(body.spans.isEmpty());
    EXPECT_EQ(body.spans.front().kind, SpanKind::Code);
}

TEST(MarkdownRules, TableRunTerminatedByBlankLine) {
    const ParseResult header = MarkdownRules::parseLine(QStringLiteral("| A | B |"), StateNone, false);
    EXPECT_EQ(header.nextFenceState, StateTable);
    const ParseResult delim =
        MarkdownRules::parseLine(QStringLiteral("| --- | --- |"), header.nextFenceState, false);
    EXPECT_EQ(delim.nextFenceState, StateTable);
    const ParseResult blank = MarkdownRules::parseLine(QString(), delim.nextFenceState, false);
    EXPECT_EQ(blank.nextFenceState, StateNone);
    EXPECT_NE(blank.kind, BlockKind::TableRow);
}

TEST(MarkdownRules, LinkTargetsForAnchorsAndUrls) {
    const ParseResult anchor = MarkdownRules::parseLine(QStringLiteral("see [a](#b)"), 0, false);
    ASSERT_EQ(anchor.links.size(), 1);
    EXPECT_EQ(anchor.links.front().target, QStringLiteral("#b"));
    bool sawAnchorText = false;
    for (const Span& span : anchor.spans) {
        if (span.kind == SpanKind::AnchorLinkText) {
            sawAnchorText = true;
        }
    }
    EXPECT_TRUE(sawAnchorText);

    const ParseResult url = MarkdownRules::parseLine(QStringLiteral("see [a](http://x)"), 0, false);
    ASSERT_EQ(url.links.size(), 1);
    EXPECT_EQ(url.links.front().target, QStringLiteral("http://x"));
    bool sawLinkText = false;
    for (const Span& span : url.spans) {
        if (span.kind == SpanKind::LinkText) {
            sawLinkText = true;
        }
    }
    EXPECT_TRUE(sawLinkText);
}

TEST(MarkdownRules, TocMarkersHideWhenUnrevealed) {
    const ParseResult openHidden =
        MarkdownRules::parseLine(QStringLiteral("<!-- toc -->"), StateNone, false);
    EXPECT_EQ(openHidden.kind, BlockKind::TocOpen);
    ASSERT_FALSE(openHidden.spans.isEmpty());
    EXPECT_EQ(openHidden.spans.front().kind, SpanKind::HiddenMarker);

    const ParseResult openShown =
        MarkdownRules::parseLine(QStringLiteral("<!-- toc -->"), StateNone, true);
    EXPECT_EQ(openShown.kind, BlockKind::TocOpen);
    EXPECT_EQ(openShown.spans.front().kind, SpanKind::Marker);

    const ParseResult closeHidden =
        MarkdownRules::parseLine(QStringLiteral("<!-- /toc -->"), StateNone, false);
    EXPECT_EQ(closeHidden.kind, BlockKind::TocClose);
    EXPECT_EQ(closeHidden.spans.front().kind, SpanKind::HiddenMarker);

    const ParseResult closeShown =
        MarkdownRules::parseLine(QStringLiteral("<!-- /toc -->"), StateNone, true);
    EXPECT_EQ(closeShown.kind, BlockKind::TocClose);
    EXPECT_EQ(closeShown.spans.front().kind, SpanKind::Marker);
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
