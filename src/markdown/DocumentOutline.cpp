#include "markdown/DocumentOutline.h"

#include "markdown/MarkdownRules.h"

#include <QRegularExpression>
#include <QSet>

namespace {

const QRegularExpression& headingRe() {
    static const QRegularExpression re(QStringLiteral(R"(^( {0,3})(#{1,6})[ \t]+(.*)$)"));
    return re;
}

const QRegularExpression& customIdRe() {
    static const QRegularExpression re(QStringLiteral(R"(\s*\{#([A-Za-z0-9_-]+)\}\s*$)"));
    return re;
}

const QRegularExpression& singleLineFenceRe() {
    static const QRegularExpression re(QStringLiteral(R"(^[ \t]{0,3}(```+|~~~+)(.*)\1[ \t]*$)"));
    return re;
}

QString uniqueSlug(const QString& base, QSet<QString>& used) {
    QString candidate = base.isEmpty() ? QStringLiteral("section") : base;
    if (!used.contains(candidate)) {
        used.insert(candidate);
        return candidate;
    }
    int suffix = 1;
    QString next = candidate + QLatin1Char('-') + QString::number(suffix);
    while (used.contains(next)) {
        ++suffix;
        next = candidate + QLatin1Char('-') + QString::number(suffix);
    }
    used.insert(next);
    return next;
}

} // namespace

QString DocumentOutline::stripMarkup(const QString& text) {
    QString out = text;
    static const QRegularExpression reImage(QStringLiteral(R"(!\[([^\]]*)\]\([^)]*\))"));
    static const QRegularExpression reLink(QStringLiteral(R"(\[([^\]]*)\]\([^)]*\))"));
    static const QRegularExpression reCode(QStringLiteral(R"(`([^`]*)`)"));
    static const QRegularExpression reMultiMarker(QStringLiteral(R"(\*\*|__|~~)"));
    out.replace(reImage, QStringLiteral("\\1"));
    out.replace(reLink, QStringLiteral("\\1"));
    out.replace(reCode, QStringLiteral("\\1"));
    out.replace(reMultiMarker, QString());
    out.remove(QLatin1Char('*'));
    out.remove(QLatin1Char('_'));
    out.remove(QLatin1Char('~'));
    return out.trimmed();
}

QString DocumentOutline::slugify(const QString& headingText) {
    QString t = stripMarkup(headingText).toLower();
    static const QRegularExpression reInvalid(QStringLiteral(R"([^a-z0-9\s-])"));
    t.replace(reInvalid, QString());
    static const QRegularExpression reSpace(QStringLiteral(R"(\s+)"));
    t.replace(reSpace, QStringLiteral("-"));
    static const QRegularExpression reDashTrim(QStringLiteral(R"(^-+|-+$)"));
    t.replace(reDashTrim, QString());
    return t;
}

QVector<OutlineEntry> DocumentOutline::build(const QStringList& lines) {
    QVector<OutlineEntry> entries;
    QSet<QString> used;
    bool inFence = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);

        if (inFence) {
            if (MarkdownRules::isFence(line)) {
                inFence = false;
            }
            continue;
        }
        if (singleLineFenceRe().match(line).hasMatch()) {
            continue;
        }
        if (MarkdownRules::isFence(line)) {
            inFence = true;
            continue;
        }

        const auto m = headingRe().match(line);
        if (!m.hasMatch()) {
            continue;
        }

        OutlineEntry entry;
        entry.blockNumber = i;
        entry.level = m.capturedLength(2);

        QString raw = m.captured(3).trimmed();
        QString customId;
        const auto idm = customIdRe().match(raw);
        if (idm.hasMatch()) {
            customId = idm.captured(1);
            raw = raw.left(idm.capturedStart(0)).trimmed();
        }

        entry.text = stripMarkup(raw);
        const QString base = customId.isEmpty() ? slugify(raw) : customId;
        entry.slug = uniqueSlug(base, used);
        entries.append(entry);
    }

    return entries;
}

QString DocumentOutline::tocMarkdown(const QVector<OutlineEntry>& entries, int maxLevel) {
    int minLevel = 6;
    for (const OutlineEntry& entry : entries) {
        if (entry.level <= maxLevel && entry.level < minLevel) {
            minLevel = entry.level;
        }
    }

    QStringList out;
    for (const OutlineEntry& entry : entries) {
        if (entry.level > maxLevel) {
            continue;
        }
        const QString indent(qMax(0, (entry.level - minLevel)) * 2, QLatin1Char(' '));
        out.append(indent + QStringLiteral("- [") + entry.text + QStringLiteral("](#") + entry.slug
                    + QStringLiteral(")"));
    }
    return out.join(QLatin1Char('\n'));
}
