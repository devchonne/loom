#include "core/RadioStation.h"

#include <QUrl>

StreamType inferStreamType(const QString& url) {
    // Only the path matters: query strings routinely carry their own dots and
    // would otherwise be mistaken for an extension.
    QString path = url.trimmed();
    const QUrl parsed(path);
    if (parsed.isValid() && !parsed.path().isEmpty()) {
        path = parsed.path();
    } else {
        const int cut = path.indexOf(QLatin1Char('?'));
        if (cut >= 0) {
            path = path.left(cut);
        }
    }

    if (path.endsWith(QStringLiteral(".pls"), Qt::CaseInsensitive)) {
        return StreamType::Pls;
    }
    // .m3u8 has to be checked before .m3u, since it also ends in "m3u8".
    if (path.endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) {
        return StreamType::M3u8;
    }
    if (path.endsWith(QStringLiteral(".m3u"), Qt::CaseInsensitive)) {
        return StreamType::M3u;
    }
    return StreamType::Direct;
}

QString streamTypeToString(StreamType type) {
    switch (type) {
    case StreamType::Pls:
        return QStringLiteral("pls");
    case StreamType::M3u:
        return QStringLiteral("m3u");
    case StreamType::M3u8:
        return QStringLiteral("m3u8");
    case StreamType::Direct:
        break;
    }
    return QStringLiteral("direct");
}

StreamType streamTypeFromString(const QString& text) {
    if (text.compare(QStringLiteral("pls"), Qt::CaseInsensitive) == 0) {
        return StreamType::Pls;
    }
    if (text.compare(QStringLiteral("m3u"), Qt::CaseInsensitive) == 0) {
        return StreamType::M3u;
    }
    if (text.compare(QStringLiteral("m3u8"), Qt::CaseInsensitive) == 0) {
        return StreamType::M3u8;
    }
    return StreamType::Direct;
}

QString stationSourceToString(StationSource source) {
    return source == StationSource::Crawler ? QStringLiteral("crawler") : QStringLiteral("manual");
}

StationSource stationSourceFromString(const QString& text) {
    return text.compare(QStringLiteral("crawler"), Qt::CaseInsensitive) == 0 ? StationSource::Crawler
                                                                            : StationSource::Manual;
}
