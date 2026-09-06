#include "core/PlaylistResolver.h"

#include <QStringList>

namespace {

// Splits on either line ending and drops surrounding whitespace, so CRLF files
// written on Windows parse the same as LF ones.
QStringList playlistLines(const QByteArray& bytes) {
    const QString text = QString::fromUtf8(bytes);
    QStringList lines;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (!line.isEmpty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

}  // namespace

QString PlaylistResolver::parsePls(const QByteArray& bytes) {
    // A .pls can list its entries out of order, so take the lowest FileN rather
    // than whichever line happens to come first.
    int bestIndex = -1;
    QString bestUrl;
    for (const QString& line : playlistLines(bytes)) {
        if (!line.startsWith(QStringLiteral("File"), Qt::CaseInsensitive)) {
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        bool ok = false;
        const int index = line.mid(4, equals - 4).toInt(&ok);
        if (!ok) {
            continue;
        }
        const QString url = line.mid(equals + 1).trimmed();
        if (url.isEmpty()) {
            continue;
        }
        if (bestIndex < 0 || index < bestIndex) {
            bestIndex = index;
            bestUrl = url;
        }
    }
    return bestUrl;
}

QString PlaylistResolver::parseM3u(const QByteArray& bytes) {
    for (const QString& line : playlistLines(bytes)) {
        // #EXTM3U, #EXTINF and friends are metadata, not stream URLs.
        if (line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        return line;
    }
    return {};
}

QString PlaylistResolver::firstStreamUrl(StreamType type, const QByteArray& bytes) {
    switch (type) {
    case StreamType::Pls:
        return parsePls(bytes);
    case StreamType::M3u:
    case StreamType::M3u8:
        return parseM3u(bytes);
    case StreamType::Direct:
        break;
    }
    return {};
}
