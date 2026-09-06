#include "core/RadioDirectory.h"

#include "core/AtomicFile.h"
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDnsLookup>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {

// Radio Browser rejects requests that do not identify themselves, so the
// user agent is mandatory rather than cosmetic.
QByteArray userAgent() {
    const QString version = QCoreApplication::applicationVersion();
    return QStringLiteral("loom/%1")
        .arg(version.isEmpty() ? QStringLiteral("dev") : version)
        .toUtf8();
}

// Shared across every station query: hide entries the directory knows are
// broken, and keep result sets small enough to render in one list.
QString commonQuery() {
    return QStringLiteral("hidebroken=true&limit=50");
}

}  // namespace

RadioDirectory::RadioDirectory(QObject* parent)
    : QObject(parent)
    , net_(new QNetworkAccessManager(this))
    // Start on a known mirror so the very first search works immediately; the
    // SRV lookup below only refines the choice.
    , host_(fallbackMirrors().constFirst()) {
    loadCachedTags();
    resolveMirror();
}

QStringList RadioDirectory::fallbackMirrors() {
    // Community-run mirrors, used when the SRV lookup cannot be completed.
    return {QStringLiteral("de2.api.radio-browser.info"),
            QStringLiteral("nl1.api.radio-browser.info"),
            QStringLiteral("at1.api.radio-browser.info"),
            QStringLiteral("fi1.api.radio-browser.info")};
}

void RadioDirectory::resolveMirror() {
    dns_ = new QDnsLookup(QDnsLookup::SRV, QStringLiteral("_api._tcp.radio-browser.info"), this);
    connect(dns_, &QDnsLookup::finished, this, [this]() {
        if (dns_->error() == QDnsLookup::NoError && !dns_->serviceRecords().isEmpty()) {
            // Records come back with priority/weight; the lowest priority wins.
            auto records = dns_->serviceRecords();
            std::stable_sort(records.begin(), records.end(),
                             [](const QDnsServiceRecord& a, const QDnsServiceRecord& b) {
                                 return a.priority() < b.priority();
                             });
            QString name = records.first().target();
            if (name.endsWith(QLatin1Char('.'))) {
                name.chop(1);
            }
            if (!name.isEmpty()) {
                host_ = name;
            }
        }
        dns_->deleteLater();
        dns_ = nullptr;
    });
    dns_->lookup();
}

void RadioDirectory::request(const QString& path, bool tagQuery) {
    send(path, tagQuery);
}

void RadioDirectory::send(const QString& path, bool tagQuery) {
    QUrl url(QStringLiteral("https://%1%2").arg(host_, path));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", userAgent());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply*& slot = tagQuery ? tagReply_ : stationReply_;
    if (slot) {
        // A newer query wins; abort the old one so its results cannot arrive
        // after the ones the user is actually waiting for.
        //
        // abort() emits finished() synchronously, which re-enters the finished
        // handler below. Clear the member first and work off a local copy:
        // otherwise the handler nulls `slot` out from under us (it is a
        // reference to the member) and the next line dereferences nullptr.
        QNetworkReply* previous = slot;
        slot = nullptr;
        previous->abort();
        previous->deleteLater();
    }
    QNetworkReply* reply = net_->get(req);
    slot = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, tagQuery]() {
        if (tagQuery) {
            onTagsFinished(reply);
        } else {
            onStationsFinished(reply);
        }
    });
}

void RadioDirectory::onStationsFinished(QNetworkReply* reply) {
    if (reply != stationReply_) {
        // Superseded by a newer search.
        reply->deleteLater();
        return;
    }
    stationReply_ = nullptr;
    const QNetworkReply::NetworkError error = reply->error();
    const QByteArray body = reply->readAll();
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (error == QNetworkReply::OperationCanceledError) {
        return;
    }
    if (error != QNetworkReply::NoError) {
        emit failed(errorText);
        return;
    }
    emit stationsReady(parseStations(body));
}

void RadioDirectory::onTagsFinished(QNetworkReply* reply) {
    if (reply != tagReply_) {
        reply->deleteLater();
        return;
    }
    tagReply_ = nullptr;
    const QNetworkReply::NetworkError error = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        // Keep whatever the cache gave us; the genre picker stays usable.
        return;
    }
    const QStringList parsed = parseTags(body);
    if (parsed.isEmpty()) {
        return;
    }
    tags_ = parsed;
    saveCachedTags();
    emit tagsReady(tags_);
}

void RadioDirectory::searchByName(const QString& name) {
    request(QStringLiteral("/json/stations/search?name=%1&order=votes&reverse=true&%2")
                .arg(QString::fromUtf8(QUrl::toPercentEncoding(name)), commonQuery()),
            false);
}

void RadioDirectory::searchByTag(const QString& tag) {
    request(QStringLiteral("/json/stations/search?tag=%1&order=votes&reverse=true&%2")
                .arg(QString::fromUtf8(QUrl::toPercentEncoding(tag)), commonQuery()),
            false);
}

void RadioDirectory::topClicked() {
    request(QStringLiteral("/json/stations/topclick?%1").arg(commonQuery()), false);
}

void RadioDirectory::topVoted() {
    request(QStringLiteral("/json/stations/topvote?%1").arg(commonQuery()), false);
}

void RadioDirectory::fetchTags() {
    // 400 covers the tags anyone would realistically browse by, and keeps the
    // picker list navigable.
    request(QStringLiteral("/json/tags?order=stationcount&reverse=true&limit=400"), true);
}

QVector<DirectoryStation> RadioDirectory::parseStations(const QByteArray& json) {
    QVector<DirectoryStation> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        return out;
    }
    for (const QJsonValue& value : doc.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        DirectoryStation station;
        station.uuid = obj.value(QStringLiteral("stationuuid")).toString();
        station.name = obj.value(QStringLiteral("name")).toString().trimmed();
        // url_resolved has already followed redirects and playlist indirection,
        // so it is preferred over the raw url when present.
        station.url = obj.value(QStringLiteral("url_resolved")).toString().trimmed();
        if (station.url.isEmpty()) {
            station.url = obj.value(QStringLiteral("url")).toString().trimmed();
        }
        station.favicon = obj.value(QStringLiteral("favicon")).toString();
        station.countryCode = obj.value(QStringLiteral("countrycode")).toString();
        station.codec = obj.value(QStringLiteral("codec")).toString();
        station.bitrate = obj.value(QStringLiteral("bitrate")).toInt(0);
        // The flag arrives as 1/0 in most responses but as a bool in some.
        const QJsonValue check = obj.value(QStringLiteral("lastcheckok"));
        station.lastCheckOk = check.isBool() ? check.toBool(true) : check.toInt(1) != 0;
        // tags is a single comma-separated string, not an array.
        const QString tags = obj.value(QStringLiteral("tags")).toString();
        for (const QString& tag : tags.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString trimmed = tag.trimmed().toLower();
            if (!trimmed.isEmpty() && !station.tags.contains(trimmed)) {
                station.tags.push_back(trimmed);
            }
        }
        if (station.name.isEmpty() || station.url.isEmpty()) {
            continue;
        }
        out.push_back(station);
    }
    return out;
}

QStringList RadioDirectory::parseTags(const QByteArray& json) {
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        return out;
    }
    for (const QJsonValue& value : doc.array()) {
        QString name;
        if (value.isObject()) {
            name = value.toObject().value(QStringLiteral("name")).toString();
        } else if (value.isString()) {
            name = value.toString();
        }
        name = name.trimmed().toLower();
        if (!name.isEmpty() && !out.contains(name)) {
            out.push_back(name);
        }
    }
    return out;
}

void RadioDirectory::loadCachedTags() {
    const QByteArray bytes = AtomicFile::read(Paths::radioTagCacheFile());
    if (bytes.isEmpty()) {
        return;
    }
    tags_ = parseTags(bytes);
}

void RadioDirectory::saveCachedTags() const {
    QJsonArray array;
    for (const QString& tag : tags_) {
        array.append(tag);
    }
    AtomicFile::write(Paths::radioTagCacheFile(), QJsonDocument(array).toJson(QJsonDocument::Compact));
}
