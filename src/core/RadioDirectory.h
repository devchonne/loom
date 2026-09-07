#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

class QDnsLookup;
class QNetworkAccessManager;
class QNetworkReply;

// One ephemeral search result from the external directory. Nothing here is
// stored until the user explicitly adds it to their library.
struct DirectoryStation {
    QString uuid;
    QString name;
    QString url;
    QString favicon;
    QString countryCode;
    QString codec;
    QStringList tags;
    int bitrate = 0;
    // The directory's own health flag. Crowdsourced data goes stale, so a
    // station that failed its last check is flagged rather than trusted.
    bool lastCheckOk = true;
};

// Read-only client for Radio Browser (radio-browser.info), the default discovery
// source. Requires no API key or account, so browsing works on first launch.
//
// The service is mirrored across community-run servers, so the host is resolved
// through a DNS SRV lookup with a hardcoded fallback list; one mirror going down
// does not break discovery.
class RadioDirectory : public QObject {
    Q_OBJECT

public:
    explicit RadioDirectory(QObject* parent = nullptr);

    void searchByName(const QString& name);
    void searchByTag(const QString& tag);
    void topClicked();
    void topVoted();

    // Tag vocabulary that constrains genre values everywhere, including the
    // manual-add form. Served from the on-disk cache first when there is one.
    void fetchTags();
    const QStringList& tags() const { return tags_; }

    // Pure parsing, exposed so the response shape is testable without network.
    static QVector<DirectoryStation> parseStations(const QByteArray& json);
    static QStringList parseTags(const QByteArray& json);

    // Mirrors used when the SRV lookup fails or is still in flight.
    static QStringList fallbackMirrors();

signals:
    void stationsReady(const QVector<DirectoryStation>& stations);
    void tagsReady(const QStringList& tags);
    void failed(const QString& message);

private:
    void resolveMirror();
    // Issues the query against the current mirror.
    void request(const QString& path, bool tagQuery);
    void send(const QString& path, bool tagQuery);
    void onStationsFinished(QNetworkReply* reply);
    void onTagsFinished(QNetworkReply* reply);
    void loadCachedTags();
    void saveCachedTags() const;

    QNetworkAccessManager* net_ = nullptr;
    QDnsLookup* dns_ = nullptr;
    // Starts on a fallback mirror so the first search works right away; the SRV
    // lookup only refines it.
    QString host_;
    // Only one search and one tag fetch can be in flight; a newer query aborts
    // the older one so fast typing cannot deliver results out of order.
    QNetworkReply* stationReply_ = nullptr;
    QNetworkReply* tagReply_ = nullptr;
    QStringList tags_;
};
