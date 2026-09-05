#include "core/PdfExport.h"

#include "core/Paths.h"
#include "theme/Fonts.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontMetricsF>
#include <QImage>
#include <QPalette>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>
#include <gtest/gtest.h>

namespace {

PdfExportRequest requestFor(const QString& templateId, const QString& markdown) {
    PdfExportRequest req;
    req.markdown = markdown;
    req.title = QStringLiteral("notes");
    req.templateId = templateId;
    return req;
}

}  // namespace

TEST(PdfTemplateCatalog, HasUniqueIdsAndBlurbs) {
    const QVector<PdfTemplate>& all = PdfTemplates::catalog();
    ASSERT_FALSE(all.isEmpty());
    QStringList seen;
    for (const PdfTemplate& tpl : all) {
        EXPECT_FALSE(tpl.id.trimmed().isEmpty());
        EXPECT_FALSE(tpl.blurb.trimmed().isEmpty());
        EXPECT_FALSE(seen.contains(tpl.id)) << tpl.id.toStdString();
        seen.append(tpl.id);
        EXPECT_GT(tpl.basePt, 0.0);
        EXPECT_GE(tpl.lineHeight, 1.0);
    }
}

TEST(PdfTemplateCatalog, DefaultIdIsInTheCatalog) {
    QStringList ids;
    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        ids.append(tpl.id);
    }
    EXPECT_TRUE(ids.contains(PdfTemplates::defaultId()));
}

TEST(PdfTemplateCatalog, NormalizeFallsBackAndIsCaseInsensitive) {
    EXPECT_EQ(PdfTemplates::normalize(QString()), PdfTemplates::defaultId());
    EXPECT_EQ(PdfTemplates::normalize(QStringLiteral("   ")), PdfTemplates::defaultId());
    EXPECT_EQ(PdfTemplates::normalize(QStringLiteral("not-a-template")), PdfTemplates::defaultId());
    EXPECT_EQ(PdfTemplates::normalize(QStringLiteral("TERMINAL")), QStringLiteral("terminal"));
    EXPECT_EQ(PdfTemplates::normalize(QStringLiteral(" Compact ")), QStringLiteral("compact"));
}

TEST(PdfTemplateCatalog, ByIdNeverFails) {
    EXPECT_EQ(PdfTemplates::byId(QStringLiteral("junk")).id, PdfTemplates::defaultId());
    EXPECT_EQ(PdfTemplates::byId(QStringLiteral("manuscript")).id, QStringLiteral("manuscript"));
    EXPECT_TRUE(PdfTemplates::byId(QStringLiteral("terminal")).useThemeColors);
    EXPECT_FALSE(PdfTemplates::byId(QStringLiteral("compact")).footer);
    EXPECT_TRUE(PdfTemplates::byId(QStringLiteral("plain")).rawHtml);
}

TEST(PdfNormalizeMarkdown, ConvertsFlatLoomCheckboxes) {
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("[] open\n[x] done\n[X] also done\n"));
    EXPECT_TRUE(out.contains(QStringLiteral("- [ ] open")));
    EXPECT_TRUE(out.contains(QStringLiteral("- [x] done")));
    EXPECT_TRUE(out.contains(QStringLiteral("- [x] also done")));
}

TEST(PdfNormalizeMarkdown, IndentsNestedLoomCheckboxes) {
    const QString out =
        PdfExport::normalizeMarkdown(QStringLiteral("[] top\n[[]] child\n[[[x]]] grandchild\n"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    ASSERT_GE(lines.size(), 3);
    EXPECT_EQ(lines.at(0), QStringLiteral("- [ ] top"));
    EXPECT_EQ(lines.at(1), QStringLiteral("  - [ ] child"));
    EXPECT_EQ(lines.at(2), QStringLiteral("    - [x] grandchild"));
}

TEST(PdfNormalizeMarkdown, IgnoresUnbalancedBrackets) {
    const QString in = QStringLiteral("[[] mismatched\n");
    EXPECT_EQ(PdfExport::normalizeMarkdown(in), in);
}

TEST(PdfNormalizeMarkdown, StripsTocMarkersButKeepsTheList) {
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("<!-- toc -->\n- [one](#one)\n<!-- /toc -->\n\n# one\n"));
    EXPECT_FALSE(out.contains(QStringLiteral("<!-- toc -->")));
    EXPECT_FALSE(out.contains(QStringLiteral("<!-- /toc -->")));
    EXPECT_TRUE(out.contains(QStringLiteral("- [one](#one)")));
    EXPECT_TRUE(out.contains(QStringLiteral("# one")));
}

TEST(PdfNormalizeMarkdown, ReplacesTheOpeningTocMarkerWithARealHeading) {
    // Regression: the editor paints "Table of Contents" as an overlay above the
    // `<!-- toc -->` marker (see Editor::paintEvent); that heading has no
    // markdown of its own, so simply stripping the marker -- as before -- left
    // the exported list floating under nothing.
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("<!-- toc -->\n- [one](#one)\n<!-- /toc -->\n\n# one\n"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    ASSERT_FALSE(lines.isEmpty());
    EXPECT_EQ(lines.first(), QStringLiteral("## Table of Contents"));
}

TEST(PdfNormalizeMarkdown, LeavesOrdinaryMarkdownAlone) {
    const QString in = QStringLiteral("# title\n\nA paragraph.\n\n| a | b |\n| --- | --- |\n");
    EXPECT_EQ(PdfExport::normalizeMarkdown(in), in);
}

TEST(PdfBuildHtml, ContainsTitleAndRenderedBody) {
    const QString html = PdfExport::buildHtml(
        requestFor(QStringLiteral("paper"), QStringLiteral("# hello\n\nbody text\n")));
    EXPECT_TRUE(html.contains(QStringLiteral("<title>notes</title>")));
    EXPECT_TRUE(html.contains(QStringLiteral("<h1>hello</h1>")));
    EXPECT_TRUE(html.contains(QStringLiteral("body text")));
}

TEST(PdfBuildHtml, EscapesTheTitle) {
    PdfExportRequest req = requestFor(QStringLiteral("paper"), QStringLiteral("hi\n"));
    req.title = QStringLiteral("a<b> & c");
    const QString html = PdfExport::buildHtml(req);
    EXPECT_FALSE(html.contains(QStringLiteral("<title>a<b>")));
    EXPECT_TRUE(html.contains(QStringLiteral("&lt;b&gt;")));
}

TEST(PdfBuildHtml, TemplatesProduceDifferentStyling) {
    const QString markdown = QStringLiteral("# hello\n\nbody\n");
    const QString paper = PdfExport::buildHtml(requestFor(QStringLiteral("paper"), markdown));
    const QString terminal = PdfExport::buildHtml(requestFor(QStringLiteral("terminal"), markdown));
    const QString plain = PdfExport::buildHtml(requestFor(QStringLiteral("plain"), markdown));
    EXPECT_NE(paper, terminal);
    EXPECT_TRUE(paper.contains(QStringLiteral("<style")));
    EXPECT_TRUE(terminal.contains(QStringLiteral("<style")));
    // plain is the bare md4c body with no wrapper stylesheet.
    EXPECT_FALSE(plain.contains(QStringLiteral("<style")));
    EXPECT_TRUE(plain.contains(QStringLiteral("<h1>hello</h1>")));
}

TEST(PdfBuildHtml, PaperIsLightAndTerminalFollowsTheTheme) {
    const QString markdown = QStringLiteral("# hello\n");
    const QString paper = PdfExport::buildHtml(requestFor(QStringLiteral("paper"), markdown));
    EXPECT_TRUE(paper.contains(QColor(Qt::white).name(), Qt::CaseInsensitive));

    PdfExportRequest req = requestFor(QStringLiteral("terminal"), markdown);
    req.theme = Theme::builtin();
    const QString terminal = PdfExport::buildHtml(req);
    EXPECT_TRUE(terminal.contains(req.theme.background.name(), Qt::CaseInsensitive));
    EXPECT_TRUE(terminal.contains(req.theme.accent.name(), Qt::CaseInsensitive));
}

TEST(PdfAdaptHtmlForQt, ReplacesCheckboxInputsQtCannotDraw) {
    const QString in = QStringLiteral(
        "<li><input type=\"checkbox\" class=\"c\" disabled>open</li>"
        "<li><input type=\"checkbox\" class=\"c\" disabled checked>done</li>");
    const QString out = PdfExport::adaptHtmlForQt(in, true);
    EXPECT_FALSE(out.contains(QStringLiteral("<input"), Qt::CaseInsensitive));
    EXPECT_TRUE(out.contains(QStringLiteral("[x]")));
    EXPECT_TRUE(out.contains(QStringLiteral("[&nbsp;]")));
}

TEST(PdfAdaptHtmlForQt, UsesAsciiBoxesRatherThanBallotGlyphs) {
    // Departure Mono has no U+2610/U+2611; the fallback font's metrics would
    // inflate the whole line, so the exporter must stay on ASCII.
    const QString out = PdfExport::adaptHtmlForQt(
        QStringLiteral("<li><input type=\"checkbox\" disabled checked>done</li>"), true);
    EXPECT_FALSE(out.contains(QChar(0x2610)));
    EXPECT_FALSE(out.contains(QChar(0x2611)));
}

TEST(PdfAdaptHtmlForQt, AddsTableBorderAttributesOnlyWhenStyled) {
    const QString in = QStringLiteral("<table>\n<tr><td>a</td></tr>\n</table>");
    EXPECT_TRUE(PdfExport::adaptHtmlForQt(in, true).contains(QStringLiteral("border=\"1\"")));
    EXPECT_FALSE(PdfExport::adaptHtmlForQt(in, false).contains(QStringLiteral("border=")));
}

TEST(PdfBuildHtml, LineHeightIsAPercentageOfTheFontSize) {
    // Regression: line-height was once computed as basePt * (lineHeight - 1) * 100,
    // which yielded percentages in the hundreds and quadrupled the page count.
    static const QRegularExpression reLineHeight(QStringLiteral(R"(line-height:\s*(\d+)%)"));
    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        if (tpl.rawHtml) {
            continue;
        }
        const QString html = PdfExport::buildHtml(requestFor(tpl.id, QStringLiteral("body\n")));
        const auto m = reLineHeight.match(html);
        ASSERT_TRUE(m.hasMatch()) << tpl.id.toStdString();
        const int percent = m.captured(1).toInt();
        EXPECT_EQ(percent, int(tpl.lineHeight * 100.0)) << tpl.id.toStdString();
        EXPECT_GE(percent, 100) << tpl.id.toStdString();
        EXPECT_LE(percent, 300) << tpl.id.toStdString();
    }
}

TEST(PdfBuildHtml, RendersCheckboxesAndTableBordersForStyledTemplates) {
    const QString html = PdfExport::buildHtml(
        requestFor(QStringLiteral("paper"),
                   QStringLiteral("[] open\n[x] done\n\n| a |\n| --- |\n| 1 |\n")));
    EXPECT_FALSE(html.contains(QStringLiteral("<input"), Qt::CaseInsensitive));
    EXPECT_TRUE(html.contains(QStringLiteral("[x]")));
    EXPECT_TRUE(html.contains(QStringLiteral("border=\"1\"")));
}

TEST(PdfBuildHtml, PadsTableCellsAndSeparatesTablesFromSurroundingText) {
    // Regression: cell text sat right against the grid lines and the table butted
    // straight into the next heading, which the editor's padded panel never does.
    static const QRegularExpression rePadX(QStringLiteral(R"(padding-left:\s*(\d+)px)"));
    static const QRegularExpression rePadY(QStringLiteral(R"(padding-top:\s*(\d+)px)"));
    static const QRegularExpression reGap(QStringLiteral(R"(table\s*\{[^}]*margin-top:\s*(\d+)px)"));
    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        if (tpl.rawHtml) {
            continue;
        }
        const QString html = PdfExport::buildHtml(
            requestFor(tpl.id, QStringLiteral("| a |\n| --- |\n| 1 |\n")));
        const auto padX = rePadX.match(html);
        const auto padY = rePadY.match(html);
        const auto gap = reGap.match(html);
        ASSERT_TRUE(padX.hasMatch()) << tpl.id.toStdString();
        ASSERT_TRUE(padY.hasMatch()) << tpl.id.toStdString();
        ASSERT_TRUE(gap.hasMatch()) << tpl.id.toStdString();
        // Horizontal room reads as wider than vertical, matching the editor.
        EXPECT_GT(padX.captured(1).toInt(), padY.captured(1).toInt()) << tpl.id.toStdString();
        EXPECT_GT(gap.captured(1).toInt(), 0) << tpl.id.toStdString();
    }
}

TEST(PdfAdaptHtmlForQt, LeavesCellPaddingToTheStylesheet) {
    // The `cellpadding` attribute takes a single value for all four sides and
    // overrides CSS, so it must not appear or the asymmetric padding is lost.
    const QString out = PdfExport::adaptHtmlForQt(
        QStringLiteral("<table>\n<tr><td>a</td></tr>\n</table>"), true);
    EXPECT_TRUE(out.contains(QStringLiteral("border=\"1\"")));
    EXPECT_FALSE(out.contains(QStringLiteral("cellpadding"), Qt::CaseInsensitive));
}

TEST(PdfNormalizeMarkdown, ConvertsStarNestingIntoIndentedLists) {
    // Regression: loom treats `**`/`***` as list nesting levels, but md4c read
    // them as emphasis, so sub-items rendered literally as "** Sub Item".
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("* Item 1\n* Item 2\n** Sub Item 2.1\n*** Sub Sub 2.1.1\n"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    ASSERT_GE(lines.size(), 4);
    EXPECT_EQ(lines.at(0), QStringLiteral("- Item 1"));
    EXPECT_EQ(lines.at(1), QStringLiteral("- Item 2"));
    EXPECT_EQ(lines.at(2), QStringLiteral("  - Sub Item 2.1"));
    EXPECT_EQ(lines.at(3), QStringLiteral("    - Sub Sub 2.1.1"));
    EXPECT_FALSE(out.contains(QStringLiteral("**")));
}

TEST(PdfNormalizeMarkdown, KeepsOtherListMarkersAndOrderedLists) {
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("- dash\n+ plus\n1. first\n2) second\n"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    EXPECT_EQ(lines.at(0), QStringLiteral("- dash"));
    EXPECT_EQ(lines.at(1), QStringLiteral("- plus"));
    EXPECT_EQ(lines.at(2), QStringLiteral("1. first"));
    EXPECT_EQ(lines.at(3), QStringLiteral("2) second"));
}

TEST(PdfNormalizeMarkdown, TreatsIndentationAsNestingLikeTheEditor) {
    const QString out =
        PdfExport::normalizeMarkdown(QStringLiteral("* top\n  * two spaces\n\t* one tab\n"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    EXPECT_EQ(lines.at(0), QStringLiteral("- top"));
    EXPECT_EQ(lines.at(1), QStringLiteral("  - two spaces"));
    EXPECT_EQ(lines.at(2), QStringLiteral("  - one tab"));
}

TEST(PdfNormalizeMarkdown, NormalizesBareBoxAfterABullet) {
    // The editor accepts `* []`, but GFM needs `- [ ]` or the box is literal text.
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("* [] todo\n* [x] done\n** [] nested\n"));
    const QStringList lines = out.split(QLatin1Char('\n'));
    EXPECT_EQ(lines.at(0), QStringLiteral("- [ ] todo"));
    EXPECT_EQ(lines.at(1), QStringLiteral("- [x] done"));
    EXPECT_EQ(lines.at(2), QStringLiteral("  - [ ] nested"));
}

TEST(PdfNormalizeMarkdown, LeavesThematicBreaksAlone) {
    // `***` is a horizontal rule, not a third-level list marker.
    for (const QString& rule : {QStringLiteral("***"), QStringLiteral("---"),
                                QStringLiteral("___"), QStringLiteral("* * *")}) {
        const QString in = rule + QLatin1Char('\n');
        const QString out = PdfExport::normalizeMarkdown(in);
        EXPECT_EQ(out.trimmed(), rule.trimmed()) << rule.toStdString();
    }
}

TEST(PdfNormalizeMarkdown, DoesNotRewriteInsideFencedCode) {
    const QString in = QStringLiteral("```\n** not a list\n[] not a checkbox\n```\n\n** real list\n");
    const QString out = PdfExport::normalizeMarkdown(in);
    EXPECT_TRUE(out.contains(QStringLiteral("** not a list")));
    EXPECT_TRUE(out.contains(QStringLiteral("[] not a checkbox")));
    // The line outside the fence is still converted.
    EXPECT_TRUE(out.contains(QStringLiteral("  - real list")));
}

TEST(PdfNormalizeMarkdown, HandlesTildeFencesAndKeepsThemBalanced) {
    const QString out = PdfExport::normalizeMarkdown(
        QStringLiteral("~~~\n** inside\n~~~\n** outside\n"));
    EXPECT_TRUE(out.contains(QStringLiteral("** inside")));
    EXPECT_TRUE(out.contains(QStringLiteral("  - outside")));
}

TEST(PdfSuppressTaskListBullets, DropsBulletsOnTaskListsOnly) {
    // Regression: checkboxes rendered with a bullet in front of them, which the
    // editor never does because the checkbox replaces the marker.
    const QString taskList = PdfExport::suppressTaskListBullets(
        QStringLiteral("<ul>\n<li class=\"task-list-item\">a</li>\n</ul>"));
    EXPECT_TRUE(taskList.contains(QStringLiteral("list-style-type: none;")));

    const QString plainList =
        PdfExport::suppressTaskListBullets(QStringLiteral("<ul>\n<li>a</li>\n</ul>"));
    EXPECT_FALSE(plainList.contains(QStringLiteral("list-style-type")));
}

TEST(PdfSuppressTaskListBullets, HandlesNestedMixedLists) {
    // Outer plain list keeps its bullets; the inner task list loses them.
    const QString html = PdfExport::suppressTaskListBullets(QStringLiteral(
        "<ul>\n<li>plain<ul>\n<li class=\"task-list-item\">task</li>\n</ul>\n</li>\n</ul>"));
    EXPECT_EQ(html.count(QStringLiteral("list-style-type: none;")), 1);
    // The marked <ul> must be the inner one, i.e. after the "plain" text.
    EXPECT_GT(html.indexOf(QStringLiteral("list-style-type")), html.indexOf(QStringLiteral("plain")));
}

TEST(PdfBuildHtml, TaskListsHaveNoBulletAndSubItemsNest) {
    const QString html = PdfExport::buildHtml(
        requestFor(QStringLiteral("paper"),
                   QStringLiteral("* Item\n** Sub Item\n\n[] task\n[[x]] nested task\n")));
    EXPECT_TRUE(html.contains(QStringLiteral("list-style-type: none;")));
    EXPECT_FALSE(html.contains(QStringLiteral("** Sub Item")));
    // The sub item became a real nested list rather than emphasis or literal text.
    EXPECT_FALSE(html.contains(QStringLiteral("<em>")));
    EXPECT_GE(html.count(QStringLiteral("<ul")), 2);
}

TEST(PdfBuildHtml, KeepsTheCheckboxOnTheSameLineInLooseLists) {
    // A blank line makes md4c emit a loose list (<p> per item); the box must end
    // up inside the paragraph or the text drops to the next line.
    const QString html = PdfExport::buildHtml(
        requestFor(QStringLiteral("paper"), QStringLiteral("[] one\n\n[x] two\n")));
    EXPECT_FALSE(html.contains(QStringLiteral("]&nbsp;<p>")));
    EXPECT_FALSE(html.contains(QStringLiteral("]&nbsp;\n<p>")));
}

TEST(PdfResolveImagePaths, MakesRelativePathsAbsoluteAgainstTheDocument) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(dir.filePath(QStringLiteral("pic.png"))));

    const QString out = PdfExport::resolveImagePaths(
        QStringLiteral("<img src=\"pic.png\" alt=\"\">"), dir.path());
    EXPECT_TRUE(out.contains(dir.path() + QStringLiteral("/pic.png")));
}

TEST(PdfResolveImagePaths, LeavesUrlsAndUnknownFilesUntouched) {
    const QString url = QStringLiteral("<img src=\"https://example.com/a.png\">");
    EXPECT_EQ(PdfExport::resolveImagePaths(url, QStringLiteral("/tmp")), url);

    const QString missing = QStringLiteral("<img src=\"nope-does-not-exist.png\">");
    EXPECT_EQ(PdfExport::resolveImagePaths(missing, QStringLiteral("/tmp")), missing);
}

TEST(PdfResolveImagePaths, HandlesSeveralImagesAndKeepsSurroundingMarkup) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::blue);
    ASSERT_TRUE(image.save(dir.filePath(QStringLiteral("a.png"))));
    ASSERT_TRUE(image.save(dir.filePath(QStringLiteral("b.png"))));

    const QString out = PdfExport::resolveImagePaths(
        QStringLiteral("<p>x<img src=\"a.png\" alt=\"A\">y<img src=\"b.png\">z</p>"), dir.path());
    EXPECT_TRUE(out.startsWith(QStringLiteral("<p>x")));
    EXPECT_TRUE(out.endsWith(QStringLiteral("z</p>")));
    EXPECT_TRUE(out.contains(QStringLiteral("alt=\"A\"")));
    EXPECT_TRUE(out.contains(dir.path() + QStringLiteral("/a.png")));
    EXPECT_TRUE(out.contains(dir.path() + QStringLiteral("/b.png")));
}

TEST(PdfWrite, EmbedsImagesRatherThanAPlaceholderIcon) {
    // Regression: a pasted image lives in the shared media dir, not next to the
    // document, so Qt's own base-url resolution missed it and painted its tiny
    // "broken image" icon instead. Compare against a deliberately absent file:
    // the real screenshot must add substantially more image data.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage image(240, 120, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    ASSERT_TRUE(image.save(dir.filePath(QStringLiteral("shot.png"))));

    auto byteSize = [&](const QString& markdown, const QString& name) {
        PdfExportRequest req = requestFor(QStringLiteral("paper"), markdown);
        req.baseDir = dir.path();
        const QString path = dir.filePath(name);
        QString err;
        EXPECT_TRUE(PdfExport::write(path, req, &err)) << err.toStdString();
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::ReadOnly));
        return file.size();
    };

    const qint64 withImage =
        byteSize(QStringLiteral("# t\n\n![](shot.png)\n"), QStringLiteral("with.pdf"));
    const qint64 withoutImage =
        byteSize(QStringLiteral("# t\n\n![](absent.png)\n"), QStringLiteral("without.pdf"));
    EXPECT_GT(withImage, withoutImage);
}

TEST(PdfResolveImagePaths, FindsPastedImagesInTheSharedMediaDir) {
    // The real regression behind the placeholder icon: `![](uuid.png)` refers to
    // Paths::mediaDir(), which is nowhere near the document.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString mediaDir = Paths::mediaDir();
    if (!QDir().mkpath(mediaDir)) {
        GTEST_SKIP() << "cannot create " << mediaDir.toStdString();
    }
    const QString name =
        QStringLiteral("loom-test-%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QImage image(6, 6, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    if (!image.save(mediaDir + QLatin1Char('/') + name)) {
        GTEST_SKIP() << "cannot write into " << mediaDir.toStdString();
    }

    // baseDir deliberately does not contain the file.
    const QString out = PdfExport::resolveImagePaths(
        QStringLiteral("<img src=\"%1\">").arg(name), dir.path());
    EXPECT_TRUE(out.contains(mediaDir + QLatin1Char('/') + name)) << out.toStdString();
    QFile::remove(mediaDir + QLatin1Char('/') + name);
}

TEST(PdfWrite, ScalesWideImagesDownToTheTextColumn) {
    // A 3000px screenshot must be scaled into the text column rather than painted
    // at its natural size, which ran off the page edge.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage wide(3000, 600, QImage::Format_RGB32);
    wide.fill(Qt::darkGreen);
    ASSERT_TRUE(wide.save(dir.filePath(QStringLiteral("wide.png"))));
    QImage small(120, 60, QImage::Format_RGB32);
    small.fill(Qt::darkGreen);
    ASSERT_TRUE(small.save(dir.filePath(QStringLiteral("small.png"))));

    // A4 at 300dpi minus 18mm side margins.
    const qreal textWidth = (210.0 - 36.0) / 25.4 * 300.0;

    PdfExportRequest req = requestFor(QStringLiteral("paper"), QStringLiteral("![](wide.png)\n"));
    req.baseDir = dir.path();
    const QVector<QSizeF> wideSizes = PdfExport::imageSizesForTest(req);
    ASSERT_EQ(wideSizes.size(), 1);
    EXPECT_LE(wideSizes.at(0).width(), textWidth + 1.0)
        << "image is " << wideSizes.at(0).width() << " device px in a " << textWidth << " px column";
    // Aspect ratio preserved (3000x600 is 5:1).
    EXPECT_NEAR(wideSizes.at(0).width() / wideSizes.at(0).height(), 5.0, 0.05);

    // An image that already fits is left at its natural size.
    req.markdown = QStringLiteral("![](small.png)\n");
    const QVector<QSizeF> smallSizes = PdfExport::imageSizesForTest(req);
    ASSERT_EQ(smallSizes.size(), 1);
    EXPECT_LT(smallSizes.at(0).width(), textWidth);
    EXPECT_NEAR(smallSizes.at(0).width() / smallSizes.at(0).height(), 2.0, 0.05);
}

TEST(PdfWrite, RejectsAnEmptyPath) {
    QString err;
    EXPECT_FALSE(PdfExport::write(QString(), requestFor(QStringLiteral("paper"),
                                                        QStringLiteral("hi\n")), &err));
    EXPECT_FALSE(err.isEmpty());
}

TEST(PdfWrite, ProducesARealPdfForEveryTemplate) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString markdown = QStringLiteral(
        "# report\n\nsome **bold** text and `code`.\n\n[] task\n[[x]] nested done\n\n"
        "| a | b |\n| --- | --- |\n| 1 | 2 |\n");

    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        const QString path = dir.filePath(tpl.id + QStringLiteral(".pdf"));
        PdfExportRequest req = requestFor(tpl.id, markdown);
        QString err;
        ASSERT_TRUE(PdfExport::write(path, req, &err)) << tpl.id.toStdString() << ": "
                                                       << err.toStdString();
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << tpl.id.toStdString();
        const QByteArray head = file.read(5);
        EXPECT_EQ(head, QByteArray("%PDF-")) << tpl.id.toStdString();
        EXPECT_GT(file.size(), 500) << tpl.id.toStdString();
    }
}

TEST(PdfWrite, ShortDocumentsStayOnOnePage) {
    // The line-height regression silently turned a one-page note into four.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString markdown = QStringLiteral(
        "# report\n\nA paragraph.\n\n[] one\n[x] two\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n");
    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        const QString path = dir.filePath(tpl.id + QStringLiteral("-short.pdf"));
        ASSERT_TRUE(PdfExport::write(path, requestFor(tpl.id, markdown))) << tpl.id.toStdString();
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        EXPECT_EQ(bytes.count(QByteArray("/Type /Page\n")), 1) << tpl.id.toStdString();
    }
}

TEST(PdfWrite, TextIsLaidOutAtDeviceDpiNotScreenDpi) {
    // Regression: without pointing the layout at the QPdfWriter, point sizes
    // resolve against screen DPI (~96) while painting at 300, so glyphs come out
    // a third of their size and far too much text fits on a page.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString markdown;
    for (int i = 0; i < 60; ++i) {
        markdown += QStringLiteral("Line %1 of a document that should overflow one A4 page.\n\n").arg(i);
    }
    const QString path = dir.filePath(QStringLiteral("dpi.pdf"));
    ASSERT_TRUE(PdfExport::write(path, requestFor(QStringLiteral("paper"), markdown)));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const int pages = file.readAll().count(QByteArray("/Type /Page\n"));
    EXPECT_GT(pages, 1) << "60 paragraphs collapsed onto " << pages << " page(s): text is undersized";
}

TEST(PdfWrite, InkDoesNotFollowTheDesktopPalette) {
    // Regression: text with no CSS colour of its own -- which is all of the
    // raw-HTML "plain" template -- was painted in QPalette::Text. Under a dark
    // system theme that is white, so the whole PDF came out white on white.
    // Exporting under opposite palettes has to produce the same page content.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QPalette original = QApplication::palette();
    auto exportUnder = [&dir](const QColor& text, const QString& name) {
        QPalette pal = QApplication::palette();
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::WindowText, text);
        QApplication::setPalette(pal);
        const QString path = dir.filePath(name);
        QString error;
        if (!PdfExport::write(path, requestFor(QStringLiteral("plain"),
                                              QStringLiteral("# Head\n\nbody text here\n")),
                              &error)) {
            return QByteArray();
        }
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QByteArray onWhite = exportUnder(QColor(Qt::white), QStringLiteral("white.pdf"));
    const QByteArray onBlack = exportUnder(QColor(Qt::black), QStringLiteral("black.pdf"));
    QApplication::setPalette(original);
    ASSERT_FALSE(onWhite.isEmpty());
    ASSERT_FALSE(onBlack.isEmpty());
    // Only the embedded document UUID and timestamps differ between two runs, so
    // the sizes match exactly unless the glyph colour changed with the palette.
    EXPECT_EQ(onWhite.size(), onBlack.size());
}

TEST(PdfWrite, NestsSubBulletsByTwoCharactersWithEvenLeading) {
    // Regression: Qt indents each list level by indentWidth() (40px by default,
    // nearly five characters), so a sub-bullet sat far right of its parent. And
    // because md4c nests the child <ul> inside the parent's <li>, Qt gave the
    // level change the outer list's paragraph spacing, making the parent-to-child
    // gap taller than the gap between siblings. Neither is reachable from CSS.
    // The nested item is deliberately last: Qt only charges the extra paragraph
    // spacing where a list *ends* at a deeper level, so a trailing sibling would
    // hide the bug.
    const auto items = PdfExport::listGeometryForTest(
        requestFor(QStringLiteral("paper"),
                   QStringLiteral("* Item 1\n* Item 2\n** Sub Item 2.1\n")));
    ASSERT_EQ(items.size(), 3);
    EXPECT_EQ(items.at(0).level, 1);
    EXPECT_EQ(items.at(2).level, 2);

    // One nesting level steps in by two characters of the body font, no more.
    const qreal step = items.at(2).textLeft - items.at(1).textLeft;
    EXPECT_GT(step, 0.0);
    const PdfTemplate& tpl = PdfTemplates::byId(QStringLiteral("paper"));
    QFont font = Fonts::body(Settings(), tpl.basePt);
    font.setPointSizeF(tpl.basePt);
    // Same 300 dpi the exporter renders at, so the metrics line up.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QPdfWriter writer(dir.filePath(QStringLiteral("metrics.pdf")));
    writer.setResolution(300);
    const qreal charWidth = QFontMetricsF(font, &writer).horizontalAdvance(QLatin1Char('0'));
    EXPECT_NEAR(step, 2.0 * charWidth, charWidth * 0.35)
        << "one level stepped in by " << (step / charWidth) << " characters";

    // Descending into a nested item costs no more leading than a plain sibling.
    const qreal siblingPitch = items.at(1).baseline - items.at(0).baseline;
    const qreal nestingPitch = items.at(2).baseline - items.at(1).baseline;
    EXPECT_GT(siblingPitch, 0.0);
    EXPECT_NEAR(nestingPitch, siblingPitch, siblingPitch * 0.02)
        << "nesting added " << (nestingPitch - siblingPitch) << " device px of leading";
}

TEST(PdfWrite, LeavesRoomForListMarkersAtEveryLevel) {
    // Regression: Qt paints a list marker in the space to the *left* of the item's
    // text, so tightening the per-level step to two characters left a top-level
    // bullet nowhere to go -- it disappeared -- and clipped wider "10." markers
    // against the page margin. Every item needs strictly more room than its
    // marker measures, ordered lists included since their markers are widest.
    const auto items = PdfExport::listGeometryForTest(
        requestFor(QStringLiteral("paper"),
                   QStringLiteral("* parent\n** child\n* parent 2\n\n1. one\n10. ten\n")));
    ASSERT_EQ(items.size(), 5);
    for (const auto& item : items) {
        EXPECT_GT(item.markerWidth, 0.0);
        EXPECT_GT(item.textLeft, item.markerWidth)
            << "level " << item.level << " marker measures " << item.markerWidth
            << " device px but only " << item.textLeft << " is free";
    }

    // Marker width tracks the font and the template's base size, so no styled
    // template may be left without room.
    for (const PdfTemplate& tpl : PdfTemplates::catalog()) {
        if (tpl.rawHtml) {
            continue;
        }
        const auto perTemplate = PdfExport::listGeometryForTest(
            requestFor(tpl.id, QStringLiteral("* parent\n** child\n\n1. one\n10. ten\n")));
        ASSERT_FALSE(perTemplate.isEmpty()) << tpl.id.toStdString();
        for (const auto& item : perTemplate) {
            EXPECT_GT(item.textLeft, item.markerWidth)
                << tpl.id.toStdString() << " level " << item.level << " clips its marker";
        }
    }
}

TEST(PdfWrite, PaginatesLongDocuments) {    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString markdown;
    for (int i = 0; i < 400; ++i) {
        markdown += QStringLiteral("paragraph %1 with enough words to take up a whole line.\n\n").arg(i);
    }
    const QString shortPath = dir.filePath(QStringLiteral("short.pdf"));
    const QString longPath = dir.filePath(QStringLiteral("long.pdf"));
    ASSERT_TRUE(PdfExport::write(shortPath, requestFor(QStringLiteral("paper"),
                                                       QStringLiteral("one line\n"))));
    ASSERT_TRUE(PdfExport::write(longPath, requestFor(QStringLiteral("paper"), markdown)));
    EXPECT_GT(QFile(longPath).size(), QFile(shortPath).size());
}
