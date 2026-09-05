#include "core/PdfExport.h"

#include "core/MarkdownExporter.h"
#include "core/Paths.h"
#include "theme/Fonts.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPageLayout>
#include <QPainter>
#include <QPalette>
#include <QPdfWriter>
#include <QRectF>
#include <QRegularExpression>
#include <QSizeF>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTextLine>
#include <QTextList>
#include <QUrl>
#include <QVector>

#include <algorithm>

namespace {

constexpr int kResolution = 300;
// Qt lays rich text images out in logical pixels and rescales them by
// logicalDpi / qt_defaultDpi(), which is 96 on every platform loom targets.
constexpr qreal kCssDpi = 96.0;

QVector<PdfTemplate> buildCatalog() {
    QVector<PdfTemplate> out;

    PdfTemplate paper;
    paper.id = QStringLiteral("paper");
    paper.blurb = QStringLiteral("white page, print-first margins");
    paper.marginsMm = QMarginsF(18, 18, 20, 18);
    paper.basePt = 10.5;
    paper.lineHeight = 1.45;
    out.append(paper);

    PdfTemplate terminal;
    terminal.id = QStringLiteral("terminal");
    terminal.blurb = QStringLiteral("dark page in the live loom theme");
    terminal.marginsMm = QMarginsF(16, 16, 18, 16);
    terminal.basePt = 10.0;
    terminal.lineHeight = 1.5;
    terminal.useThemeColors = true;
    out.append(terminal);

    PdfTemplate manuscript;
    manuscript.id = QStringLiteral("manuscript");
    manuscript.blurb = QStringLiteral("double spaced, wide margins, for edits");
    manuscript.marginsMm = QMarginsF(25, 25, 25, 25);
    manuscript.basePt = 12.0;
    manuscript.lineHeight = 2.0;
    out.append(manuscript);

    PdfTemplate compact;
    compact.id = QStringLiteral("compact");
    compact.blurb = QStringLiteral("tight and small, for reference sheets");
    compact.marginsMm = QMarginsF(10, 10, 10, 10);
    compact.basePt = 9.0;
    compact.lineHeight = 1.25;
    compact.footer = false;
    out.append(compact);

    PdfTemplate plain;
    plain.id = QStringLiteral("plain");
    plain.blurb = QStringLiteral("no styling at all, raw markdown html");
    plain.marginsMm = QMarginsF(18, 18, 18, 18);
    plain.footer = false;
    plain.rawHtml = true;
    out.append(plain);

    return out;
}

struct Ink {
    QColor page;
    QColor body;
    QColor heading;
    QColor muted;
    QColor rule;
    QColor codeBackground;
    QColor link;
};

Ink inkFor(const PdfTemplate& tpl, const Theme& theme) {
    Ink ink;
    if (tpl.useThemeColors) {
        ink.page = theme.background;
        ink.body = theme.foreground;
        ink.heading = theme.accent;
        ink.muted = theme.muted;
        ink.rule = theme.lighterBackground;
        ink.codeBackground = theme.darkerBackground;
        ink.link = theme.blue;
        return ink;
    }
    ink.page = QColor(Qt::white);
    ink.body = QColor(26, 26, 26);
    ink.heading = QColor(0, 0, 0);
    ink.muted = QColor(110, 110, 110);
    ink.rule = QColor(200, 200, 200);
    ink.codeBackground = QColor(243, 243, 243);
    ink.link = QColor(20, 70, 150);
    return ink;
}

QString stylesheet(const PdfTemplate& tpl, const Ink& ink, const QString& family, double basePt) {
    // Qt's rich text engine only understands a subset of CSS 2.1, so this stays
    // on font, colour, margin and border properties.
    const double h1 = basePt * 1.9;
    const double h2 = basePt * 1.55;
    const double h3 = basePt * 1.3;
    const double small = basePt * 0.9;
    // CSS line-height as a plain percentage of the font size. Multiplying by the
    // point size instead would yield percentages in the hundreds.
    const int linePercent = qBound(100, int(tpl.lineHeight * 100.0), 300);
    // Table breathing room, in the logical pixels Qt rescales by
    // logicalDpi / kCssDpi -- so these read as fractions of the body em rather
    // than as device dots. The editor draws tables in a padded panel with a
    // char's worth of room beside each cell's text and a clear gap from the
    // surrounding prose; without this a table butts straight into the next
    // heading and its text touches the grid lines.
    const double em = basePt * kCssDpi / 72.0;
    const int cellPadX = qMax(2, qRound(em * 0.6));
    const int cellPadY = qMax(1, qRound(em * 0.3));
    const int tableGap = qMax(4, qRound(em * 0.9));

    QString css;
    css += QStringLiteral("body { font-family: '%1'; font-size: %2pt; color: %3; }\n")
               .arg(family)
               .arg(basePt, 0, 'f', 1)
               .arg(ink.body.name());
    css += QStringLiteral("p, li { line-height: %1%; }\n").arg(linePercent);
    css += QStringLiteral("h1 { font-size: %1pt; color: %2; }\n").arg(h1, 0, 'f', 1).arg(ink.heading.name());
    css += QStringLiteral("h2 { font-size: %1pt; color: %2; }\n").arg(h2, 0, 'f', 1).arg(ink.heading.name());
    css += QStringLiteral("h3, h4, h5, h6 { font-size: %1pt; color: %2; }\n")
               .arg(h3, 0, 'f', 1)
               .arg(ink.heading.name());
    css += QStringLiteral("a { color: %1; }\n").arg(ink.link.name());
    css += QStringLiteral("code, pre { font-family: '%1'; font-size: %2pt; background-color: %3; }\n")
               .arg(family)
               .arg(small, 0, 'f', 1)
               .arg(ink.codeBackground.name());
    css += QStringLiteral("pre { border: 1px solid %1; }\n").arg(ink.rule.name());
    css += QStringLiteral("blockquote { color: %1; border-left: 2px solid %2; }\n")
               .arg(ink.muted.name(), ink.rule.name());
    css += QStringLiteral("hr { border: 1px solid %1; }\n").arg(ink.rule.name());
    css += QStringLiteral("th { color: %1; }\n").arg(ink.heading.name());
    css += QStringLiteral("table { border-color: %1; margin-top: %2px; margin-bottom: %2px; }\n")
               .arg(ink.rule.name())
               .arg(tableGap);
    // Per-cell padding, not the <table cellpadding> attribute: that one is a
    // single value for all four sides, and cells need far more room left and
    // right than above and below. Qt's parser maps these onto
    // QTextTableCellFormat and then ignores cellpadding entirely.
    css += QStringLiteral("td, th { padding-left: %1px; padding-right: %1px; "
                          "padding-top: %2px; padding-bottom: %2px; }\n")
               .arg(cellPadX)
               .arg(cellPadY);
    if (!tpl.extraCss.isEmpty()) {
        css += tpl.extraCss;
        css += QLatin1Char('\n');
    }
    return css;
}

// Scales oversized images down to the text width, keeping their aspect ratio.
// Qt does not honour max-width in rich text, so sizes are pinned on the image
// char formats. Note the units: QTextImageFormat sizes are logical ("CSS")
// pixels which Qt then multiplies by logicalDpi / kCssDpi, so maxLogicalWidth
// must be given in the same logical space rather than device pixels.
void constrainImages(QTextDocument* doc, qreal maxLogicalWidth) {
    if (!doc || maxLogicalWidth <= 0) {
        return;
    }
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); it != block.end(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            QTextImageFormat format = fragment.charFormat().toImageFormat();
            QSizeF size(format.width(), format.height());
            if (size.width() <= 0 || size.height() <= 0) {
                const QImage image(format.name());
                if (image.isNull()) {
                    continue;
                }
                size = QSizeF(image.size());
            }
            if (size.width() <= maxLogicalWidth) {
                continue;
            }
            format.setWidth(maxLogicalWidth);
            format.setHeight(size.height() * maxLogicalWidth / size.width());
            QTextCursor cursor(doc);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
}

// Pulls nested lists in to a two-character step, reserves room for the markers,
// and drops the extra leading that Qt inserts where a list changes level.
//
// Three Qt behaviours are at work, none of them reachable from CSS:
//  * Every nesting level is inset by QTextDocument::indentWidth(), which defaults
//    to 40 logical px -- nearly five characters at the body size, so a sub-bullet
//    landed far to the right of its parent.
//  * Markers are painted in the space to the *left* of the item's text, so once
//    the step is only two characters a top-level item has no room for its own
//    bullet: it vanished, and a wider "10." marker was clipped by the page
//    margin. The left margin below buys that space back without widening the
//    per-level step.
//  * md4c nests a child <ul> *inside* the parent's <li>, and Qt gives the block
//    that opens or closes such a list the paragraph spacing of the outer list
//    item, so the parent-to-child gap came out visibly taller than the gap
//    between siblings.
void tightenLists(QTextDocument* doc, qreal indentPx, qreal markerPx) {
    if (!doc) {
        return;
    }
    doc->setIndentWidth(indentPx);
    QTextCursor cursor(doc);
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (!block.textList()) {
            continue;
        }
        QTextBlockFormat format = block.blockFormat();
        if (qFuzzyIsNull(format.topMargin()) && qFuzzyIsNull(format.bottomMargin())
            && qFuzzyCompare(format.leftMargin(), markerPx)) {
            continue;
        }
        format.setTopMargin(0);
        format.setBottomMargin(0);
        format.setLeftMargin(markerPx);
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
}

// Shared setup so write() and imageSizesForTest() lay text out identically.
void prepareDocument(QTextDocument* doc, const PdfExportRequest& request, const PdfTemplate& tpl,
                     QPaintDevice* device, qreal textWidth) {
    QFont docFont = Fonts::body(request.settings, tpl.basePt);
    docFont.setPointSizeF(tpl.basePt);
    doc->setDocumentMargin(0);
    doc->setDefaultFont(docFont);
    // Without this the layout resolves point sizes against screen DPI (~96) while
    // painting into a 300 DPI device, shrinking every glyph to a third of its size.
    doc->documentLayout()->setPaintDevice(device);
    if (!request.baseDir.isEmpty()) {
        doc->setBaseUrl(QUrl::fromLocalFile(request.baseDir + QLatin1Char('/')));
    }
    doc->setHtml(PdfExport::buildHtml(request));
    // A sub-bullet sits two characters in from its parent, as it does in the
    // editor, rather than Qt's default 40px-per-level. Three characters of left
    // margin leave room for the markers, which Qt paints to the left of the text
    // and would otherwise drop or clip.
    const QFontMetricsF metrics(docFont, device);
    const qreal charWidth = metrics.horizontalAdvance(QLatin1Char('0'));
    tightenLists(doc, 2.0 * charWidth * kCssDpi / kResolution, 3.0 * charWidth);
    // The editor scales images down to its viewport; do the same against the text
    // width so a wide screenshot cannot bleed off the page. Converted into the
    // logical pixel space that QTextImageFormat sizes live in.
    constrainImages(doc, textWidth * kCssDpi / kResolution);
}

}  // namespace

namespace PdfTemplates {

const QVector<PdfTemplate>& catalog() {
    static const QVector<PdfTemplate> cached = buildCatalog();
    return cached;
}

QString defaultId() {
    return QStringLiteral("paper");
}

QString normalize(const QString& id) {
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty()) {
        return defaultId();
    }
    for (const PdfTemplate& tpl : catalog()) {
        if (tpl.id.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return tpl.id;
        }
    }
    return defaultId();
}

const PdfTemplate& byId(const QString& id) {
    const QString wanted = normalize(id);
    const QVector<PdfTemplate>& all = catalog();
    for (const PdfTemplate& tpl : all) {
        if (tpl.id == wanted) {
            return tpl;
        }
    }
    return all.first();
}

}  // namespace PdfTemplates

QString PdfExport::normalizeMarkdown(const QString& markdown) {
    // loom's list and checkbox syntax is its own: `*`/`**`/`***` are *nesting
    // levels* rather than emphasis, and `[]`/`[[x]]` are bare checkboxes with no
    // bullet. md4c only speaks CommonMark + GFM, so translate first. See
    // MarkdownRules.cpp (reList / reChecklist) for the editor's rules.
    static const QRegularExpression reFence(QStringLiteral(R"(^[ \t]{0,3}(`{3,}|~{3,}))"));
    // CommonMark allows spaces inside a thematic break (`* * *`), which the
    // editor's tighter rule does not cover but md4c does.
    static const QRegularExpression reRule(
        QStringLiteral(R"(^ {0,3}([-*_])(?:[ \t]*\1){2,}[ \t]*$)"));
    static const QRegularExpression reChecklist(
        QStringLiteral(R"(^([ \t]*)(\[{1,6})([xX]?)(\]{1,6})[ \t]+(.*)$)"));
    static const QRegularExpression reList(
        QStringLiteral(R"(^([ \t]*)(\*{1,6}|[+\-]|\d+[.)])([ \t]+)(?:(\[[ xX]?\])([ \t]+))?(.*)$)"));
    static const QRegularExpression reTocOpenMarker(
        QStringLiteral(R"(^[ \t]*<!--[ \t]*toc[ \t]*-->[ \t]*$)"));
    static const QRegularExpression reTocCloseMarker(
        QStringLiteral(R"(^[ \t]*<!--[ \t]*/toc[ \t]*-->[ \t]*$)"));

    // The editor counts a tab as two indent columns and two columns per level.
    auto indentColumns = [](const QString& whitespace) {
        int columns = 0;
        for (const QChar c : whitespace) {
            columns += (c == QLatin1Char('\t')) ? 2 : 1;
        }
        return columns;
    };
    // Two spaces per level: enough for md4c to nest a `- ` item (its content
    // starts at column 2) inside its parent. This only governs the PDF export's
    // own markdown-to-HTML pass; the editor renders loom's `*`/`**`/`***` levels
    // directly and is untouched by this constant.
    auto indentFor = [](int level) { return QString(qMax(0, level) * 2, QLatin1Char(' ')); };

    QStringList out;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    out.reserve(lines.size());
    QString openFence;
    for (const QString& line : lines) {
        // Never rewrite the inside of a fenced code block: a sample containing
        // `** x` or `[] x` has to survive verbatim.
        const auto fence = reFence.match(line);
        if (!openFence.isEmpty()) {
            out.append(line);
            if (fence.hasMatch() && fence.captured(1).at(0) == openFence.at(0)
                && fence.captured(1).size() >= openFence.size()) {
                openFence.clear();
            }
            continue;
        }
        if (fence.hasMatch()) {
            openFence = fence.captured(1);
            out.append(line);
            continue;
        }

        // The `<!-- toc -->`/`<!-- /toc -->` pair is invisible markup: the editor
        // paints "Table of Contents" as a heading-styled overlay directly above
        // the opening marker (see Editor::paintEvent), so that heading exists
        // nowhere in the document text. Splice in a real one here or the
        // exported list would float under nothing.
        if (reTocOpenMarker.match(line).hasMatch()) {
            out.append(QStringLiteral("## Table of Contents"));
            continue;
        }
        if (reTocCloseMarker.match(line).hasMatch()) {
            continue;
        }
        // Thematic breaks such as `***` must be checked before the list rules,
        // exactly as the editor orders them.
        if (reRule.match(line).hasMatch()) {
            out.append(line);
            continue;
        }

        if (const auto m = reChecklist.match(line);
            m.hasMatch() && m.capturedLength(2) == m.capturedLength(4)) {
            const int level = indentColumns(m.captured(1)) / 2 + m.capturedLength(2) - 1;
            const QString state = m.captured(3).isEmpty() ? QStringLiteral(" ") : QStringLiteral("x");
            out.append(indentFor(level) + QStringLiteral("- [") + state + QStringLiteral("] ")
                       + m.captured(5));
            continue;
        }

        if (const auto m = reList.match(line); m.hasMatch()) {
            const QString marker = m.captured(2);
            const bool ordered = marker.at(0).isDigit();
            const int starExtra =
                (!ordered && marker.startsWith(QLatin1Char('*'))) ? int(marker.size()) - 1 : 0;
            const int level = indentColumns(m.captured(1)) / 2 + starExtra;
            // GFM needs `[ ]`; loom also accepts a bare `[]` after a bullet.
            QString box;
            if (m.capturedLength(4) > 0) {
                const QString inner = m.captured(4).mid(1, m.capturedLength(4) - 2).trimmed();
                box = QStringLiteral("[%1] ").arg(inner.isEmpty() ? QStringLiteral(" ") : inner);
            }
            out.append(indentFor(level) + (ordered ? marker : QStringLiteral("-"))
                       + QLatin1Char(' ') + box + m.captured(6));
            continue;
        }

        out.append(line);
    }
    return out.join(QLatin1Char('\n'));
}

QString PdfExport::resolveImagePaths(const QString& html, const QString& baseDir) {
    // Mirrors Editor::resolveImagePath so an exported document finds the same
    // files the editor shows: absolute, then next to the document, then the
    // shared media directory where pasted images land.
    static const QRegularExpression reSrc(QStringLiteral(R"=(src="([^"]*)")="));
    static const QRegularExpression reScheme(QStringLiteral(R"(^[a-zA-Z][a-zA-Z0-9+.\-]*:)"));

    QString out;
    out.reserve(html.size());
    int cursor = 0;
    auto it = reSrc.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString spec = m.captured(1);
        out += html.mid(cursor, m.capturedStart(0) - cursor);
        cursor = int(m.capturedEnd(0));

        QString resolved;
        if (!spec.isEmpty() && !reScheme.match(spec).hasMatch()) {
            const QFileInfo given(spec);
            if (given.isAbsolute()) {
                if (given.exists()) {
                    resolved = given.absoluteFilePath();
                }
            } else {
                if (!baseDir.isEmpty()) {
                    const QFileInfo rel(baseDir + QLatin1Char('/') + spec);
                    if (rel.exists()) {
                        resolved = rel.absoluteFilePath();
                    }
                }
                if (resolved.isEmpty()) {
                    const QFileInfo media(Paths::mediaDir() + QLatin1Char('/')
                                          + QFileInfo(spec).fileName());
                    if (media.exists()) {
                        resolved = media.absoluteFilePath();
                    }
                }
            }
        }
        out += QStringLiteral("src=\"%1\"").arg(resolved.isEmpty() ? spec : resolved);
    }
    out += html.mid(cursor);
    return out;
}


QString PdfExport::adaptHtmlForQt(const QString& html, bool styled) {
    // Qt's rich text engine silently drops <input>, so GFM task list items would
    // lose their box entirely. ASCII boxes rather than U+2610/U+2611: the bundled
    // Departure Mono has no ballot glyphs, and the fallback font's metrics
    // inflate the whole line.
    static const QRegularExpression reCheckedBox(
        QStringLiteral(R"(<input[^>]*type="checkbox"[^>]*\bchecked\b[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reBox(QStringLiteral(R"(<input[^>]*type="checkbox"[^>]*>)"),
                                          QRegularExpression::CaseInsensitiveOption);

    QString out = html;
    out.replace(reCheckedBox, QStringLiteral("[x]&nbsp;"));
    out.replace(reBox, QStringLiteral("[&nbsp;]&nbsp;"));
    // A blank line anywhere in the list makes md4c emit a "loose" list, wrapping
    // each item's text in <p>. That would push the text onto the line below its
    // own checkbox, so pull the box inside the paragraph.
    static const QRegularExpression reBoxBeforeParagraph(
        QStringLiteral(R"((\[(?:x|&nbsp;)\]&nbsp;)\s*<p>)"), QRegularExpression::CaseInsensitiveOption);
    out.replace(reBoxBeforeParagraph, QStringLiteral("<p>\\1"));
    if (styled) {
        // Qt draws table grids from the HTML attributes, not from CSS borders.
        // Cell padding deliberately stays out of the attributes: `cellpadding`
        // takes one value for all four sides and wins over the stylesheet, so
        // the asymmetric `td, th { padding-* }` rules would never apply.
        out.replace(QStringLiteral("<table>"),
                    QStringLiteral("<table border=\"1\" cellspacing=\"0\">"), Qt::CaseInsensitive);
    }
    return suppressTaskListBullets(out);
}

QString PdfExport::suppressTaskListBullets(const QString& html) {
    // The editor draws a checkbox *instead of* a bullet, so a task list must not
    // get one in the PDF either. Qt keeps a single list format per <ul>, so
    // `list-style-type` has to sit on the <ul> element: styling the <li> or using
    // a CSS class selector is ignored. Mark only those <ul>s whose first item is
    // a task item, matching how md4c groups them.
    static const QRegularExpression reListItem(
        QStringLiteral(R"(<(ul|ol)>|</(ul|ol)>|<li(?:\s[^>]*)?>)"),
        QRegularExpression::CaseInsensitiveOption);

    struct Level {
        int tagStart = 0;
        bool sawItem = false;
        bool taskList = false;
    };

    QVector<Level> stack;
    QVector<int> bulletless;
    auto it = reListItem.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString token = m.captured(0);
        if (token.startsWith(QStringLiteral("</"), Qt::CaseInsensitive)) {
            if (!stack.isEmpty()) {
                const Level level = stack.takeLast();
                if (level.taskList) {
                    bulletless.append(level.tagStart);
                }
            }
            continue;
        }
        if (token.startsWith(QStringLiteral("<li"), Qt::CaseInsensitive)) {
            if (!stack.isEmpty() && !stack.last().sawItem) {
                stack.last().sawItem = true;
                stack.last().taskList = token.contains(QStringLiteral("task-list-item"),
                                                       Qt::CaseInsensitive);
            }
            continue;
        }
        stack.append(Level{int(m.capturedStart(0)), false, false});
    }
    if (bulletless.isEmpty()) {
        return html;
    }

    // Patch from the back so earlier offsets stay valid.
    std::sort(bulletless.begin(), bulletless.end());
    QString out = html;
    for (auto i = bulletless.crbegin(); i != bulletless.crend(); ++i) {
        const int tagStart = *i;
        const int close = out.indexOf(QLatin1Char('>'), tagStart);
        if (close < 0) {
            continue;
        }
        out.insert(close, QStringLiteral(" style=\"list-style-type: none;\""));
    }
    return out;
}

QString PdfExport::buildHtml(const PdfExportRequest& request) {
    const PdfTemplate& tpl = PdfTemplates::byId(request.templateId);
    const QString body = resolveImagePaths(
        adaptHtmlForQt(MarkdownExporter::toHtml(normalizeMarkdown(request.markdown)), !tpl.rawHtml),
        request.baseDir);
    QString title = request.title.trimmed();
    if (title.isEmpty()) {
        title = QStringLiteral("loom");
    }
    const QString escapedTitle = title.toHtmlEscaped();

    if (tpl.rawHtml) {
        return QStringLiteral("<!DOCTYPE html>\n<html><head><title>%1</title></head>\n<body>\n%2\n"
                              "</body></html>\n")
            .arg(escapedTitle, body);
    }

    const Ink ink = inkFor(tpl, request.theme);
    const QFont bodyFont = Fonts::body(request.settings, tpl.basePt);
    const QString family = bodyFont.family().isEmpty() ? request.settings.bodyFont : bodyFont.family();

    return QStringLiteral("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>%1</title>\n"
                          "<style type=\"text/css\">\n%2</style></head>\n"
                          "<body bgcolor=\"%3\">\n%4\n</body></html>\n")
        .arg(escapedTitle, stylesheet(tpl, ink, family, tpl.basePt), ink.page.name(), body);
}

QVector<PdfExport::ListItemGeometry> PdfExport::listGeometryForTest(const PdfExportRequest& request) {
    const PdfTemplate& tpl = PdfTemplates::byId(request.templateId);
    QTemporaryFile scratch;
    if (!scratch.open()) {
        return {};
    }
    QPdfWriter writer(scratch.fileName());
    writer.setPageSize(QPageSize(tpl.pageSize));
    writer.setPageMargins(tpl.marginsMm, QPageLayout::Millimeter);
    writer.setResolution(kResolution);
    QPainter painter;
    if (!painter.begin(&writer)) {
        return {};
    }
    QTextDocument doc;
    prepareDocument(&doc, request, tpl, &writer, painter.viewport().width());

    QVector<ListItemGeometry> out;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QTextList* list = block.textList();
        if (!list) {
            continue;
        }
        const QTextLayout* layout = block.layout();
        if (!layout || layout->lineCount() == 0) {
            continue;
        }
        const QRectF bounds = doc.documentLayout()->blockBoundingRect(block);
        const QTextLine line = layout->lineAt(0);
        ListItemGeometry geometry;
        geometry.level = list->format().indent();
        geometry.textLeft = bounds.left() + line.x();
        geometry.baseline = bounds.top() + line.y() + line.ascent();
        // Same string and font Qt uses to paint the marker, so the width it needs
        // can be compared against the space actually left for it.
        geometry.markerWidth = QFontMetricsF(block.charFormat().font(), &writer)
                                   .horizontalAdvance(list->itemText(block));
        out.append(geometry);
    }
    painter.end();
    return out;
}

QVector<QSizeF> PdfExport::imageSizesForTest(const PdfExportRequest& request) {
    const PdfTemplate& tpl = PdfTemplates::byId(request.templateId);
    QTemporaryFile scratch;
    if (!scratch.open()) {
        return {};
    }
    QPdfWriter writer(scratch.fileName());
    writer.setPageSize(QPageSize(tpl.pageSize));
    writer.setPageMargins(tpl.marginsMm, QPageLayout::Millimeter);
    writer.setResolution(kResolution);
    QPainter painter;
    if (!painter.begin(&writer)) {
        return {};
    }
    const qreal textWidth = painter.viewport().width();
    QTextDocument doc;
    prepareDocument(&doc, request, tpl, &writer, textWidth);

    QVector<QSizeF> sizes;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); it != block.end(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isImageFormat()) {
                continue;
            }
            const QTextImageFormat format = fragment.charFormat().toImageFormat();
            QSizeF size(format.width(), format.height());
            if (size.width() <= 0 || size.height() <= 0) {
                // Images that fit are left unpinned, so fall back to intrinsic size.
                const QImage image(format.name());
                if (image.isNull()) {
                    continue;
                }
                size = QSizeF(image.size());
            }
            // Report device units so callers can compare against the text column.
            sizes.append(size * (kResolution / kCssDpi));
        }
    }
    painter.end();
    return sizes;
}

bool PdfExport::write(const QString& path, const PdfExportRequest& request, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (path.isEmpty()) {
        return fail(QStringLiteral("no output path"));
    }

    const PdfTemplate& tpl = PdfTemplates::byId(request.templateId);
    const Ink ink = inkFor(tpl, request.theme);

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(tpl.pageSize));
    writer.setPageMargins(tpl.marginsMm, QPageLayout::Millimeter);
    writer.setResolution(kResolution);
    writer.setTitle(request.title.isEmpty() ? QStringLiteral("loom") : request.title);
    writer.setCreator(QStringLiteral("loom"));

    QPainter painter;
    if (!painter.begin(&writer)) {
        return fail(QStringLiteral("could not open %1 for writing").arg(path));
    }

    QFont docFont = Fonts::body(request.settings, tpl.basePt);
    docFont.setPointSizeF(tpl.basePt);

    // painter.viewport() is already inset by the page margins.
    const QRectF viewport(painter.viewport());
    QTextDocument doc;
    prepareDocument(&doc, request, tpl, &writer, viewport.width());

    QFont footerFont = docFont;
    footerFont.setPointSizeF(qMax(6.0, tpl.basePt * 0.8));
    const QFontMetricsF footerMetrics(footerFont, &writer);
    const qreal footerHeight = tpl.footer ? footerMetrics.height() * 2.2 : 0.0;
    const qreal bodyHeight = qMax(qreal(1), viewport.height() - footerHeight);

    doc.setPageSize(QSizeF(viewport.width(), bodyHeight));
    const int pages = qMax(1, doc.pageCount());

    // Full-bleed rectangle in painter coordinates: the paint rect starts at the
    // margin offset, so shifting the full page rect back by it lands at (0,0).
    const QRectF fullRect(writer.pageLayout().fullRectPixels(kResolution));
    const QRectF paintRect(writer.pageLayout().paintRectPixels(kResolution));
    const QRectF bleed = fullRect.translated(-paintRect.topLeft());

    for (int page = 0; page < pages; ++page) {
        if (page > 0) {
            writer.newPage();
        }
        if (tpl.useThemeColors) {
            painter.fillRect(bleed, ink.page);
        }

        const QRectF slice(0, page * bodyHeight, viewport.width(), bodyHeight);
        painter.save();
        painter.setClipRect(QRectF(0, 0, viewport.width(), bodyHeight));
        painter.translate(0, -slice.top());
        // Painted through the layout rather than drawContents() so the ink for
        // text that carries no CSS colour of its own is pinned here. That is
        // everything in the raw-HTML template, and drawContents() would leave it
        // on the desktop palette's Text colour: white on a dark system theme,
        // i.e. an invisible "plain" PDF.
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.clip = slice;
        ctx.palette.setColor(QPalette::Text, ink.body);
        doc.documentLayout()->draw(&painter, ctx);
        painter.restore();

        if (!tpl.footer) {
            continue;
        }
        painter.save();
        painter.setFont(footerFont);
        painter.setPen(ink.muted);
        const QString label = QStringLiteral("%1  ·  %2 / %3")
                                  .arg(request.title.isEmpty() ? QStringLiteral("loom") : request.title)
                                  .arg(page + 1)
                                  .arg(pages);
        painter.drawText(QRectF(0, bodyHeight, viewport.width(), footerHeight),
                         Qt::AlignBottom | Qt::AlignRight, label);
        painter.restore();
    }

    painter.end();
    return true;
}
