#pragma once

#include "core/Settings.h"
#include "theme/Theme.h"

#include <QMarginsF>
#include <QPageSize>
#include <QSizeF>
#include <QString>
#include <QVector>

// One named look for an exported PDF: page geometry plus the typography and
// colour choices that get baked into the wrapper HTML.
struct PdfTemplate {
    QString id;
    QString blurb;
    QPageSize::PageSizeId pageSize = QPageSize::A4;
    // Top / right / bottom / left, in millimetres.
    QMarginsF marginsMm{18, 18, 20, 18};
    double basePt = 10.5;
    double lineHeight = 1.45;
    // Ink and page fill follow the live loom theme instead of print defaults.
    bool useThemeColors = false;
    // Draw "title  ·  page N of M" under the text body.
    bool footer = true;
    // Bare md4c output: no wrapper stylesheet at all.
    bool rawHtml = false;
    QString extraCss;
};

namespace PdfTemplates {

// Every built-in template, in picker order.
const QVector<PdfTemplate>& catalog();

// Template used when nothing has been chosen yet.
QString defaultId();

// Maps an empty or unknown id (case-insensitively) onto defaultId().
QString normalize(const QString& id);

// Never fails: falls back to the default template.
const PdfTemplate& byId(const QString& id);

}  // namespace PdfTemplates

struct PdfExportRequest {
    QString markdown;
    QString title;
    QString templateId;
    // Resolves relative image paths in the document.
    QString baseDir;
    Theme theme = Theme::builtin();
    Settings settings;
};

class PdfExport {
public:
    // Rewrites loom-only syntax (bare/nested checkboxes, toc markers) into
    // GitHub-flavoured markdown that md4c understands.
    static QString normalizeMarkdown(const QString& markdown);

    // Patches the md4c output for Qt's rich text engine, which drops
    // <input type="checkbox"> and needs border attributes to draw table grids.
    static QString adaptHtmlForQt(const QString& html, bool styled);

    // Drops the bullet from task lists, which draw a checkbox instead.
    static QString suppressTaskListBullets(const QString& html);

    // Rewrites relative <img src> values to absolute paths using the same search
    // order as the editor: absolute, then baseDir, then the shared media dir.
    static QString resolveImagePaths(const QString& html, const QString& baseDir);

    // Full standalone HTML document for the request's template. Pure, so the
    // interesting half of the exporter is unit testable without a painter.
    static QString buildHtml(const PdfExportRequest& request);

    // Expose the laid-out size of each image, so tests can prove wide screenshots
    // are scaled into the text column instead of running off the page.
    static QVector<QSizeF> imageSizesForTest(const PdfExportRequest& request);

    // Where one list item's text actually landed once Qt had laid it out.
    struct ListItemGeometry {
        // Nesting depth, 1 for a top-level item.
        int level = 0;
        // Left edge of the item's text and the baseline of its first line, both
        // in device pixels at the export resolution.
        qreal textLeft = 0;
        qreal baseline = 0;
        // Width the marker ("1." or a bullet) needs, in the same device pixels.
        // Qt paints markers in the space left of textLeft, so anything wider than
        // textLeft is clipped or dropped entirely.
        qreal markerWidth = 0;
    };

    // Expose list geometry so tests can pin the indent step and prove a nested
    // item is not given more leading than a sibling.
    static QVector<ListItemGeometry> listGeometryForTest(const PdfExportRequest& request);

    static bool write(const QString& path, const PdfExportRequest& request, QString* error = nullptr);
};
