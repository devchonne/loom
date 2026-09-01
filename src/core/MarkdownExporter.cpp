#include "core/MarkdownExporter.h"

#include <md4c-html.h>

#include <QByteArray>

QString MarkdownExporter::toHtml(const QString& markdown) {
    const QByteArray utf8 = markdown.toUtf8();
    QByteArray html;
    auto append = [](const MD_CHAR* data, MD_SIZE size, void* userdata) {
        auto* out = static_cast<QByteArray*>(userdata);
        out->append(data, int(size));
    };
    md_html(utf8.constData(), MD_SIZE(utf8.size()), append, &html, MD_DIALECT_GITHUB, 0);
    return QString::fromUtf8(html);
}
