#include "core/RadioDirectory.h"

#include "core/RadioPlayer.h"

#include <QRegularExpression>
#include <gtest/gtest.h>

namespace {

// A trimmed-down version of a real Radio Browser /json/stations response.
QByteArray sampleResponse() {
    return R"([
  {
    "stationuuid": "abc-123",
    "name": "  Groove Salad  ",
    "url": "https://ice.example/groovesalad.pls",
    "url_resolved": "https://ice1.example/groovesalad-128-mp3",
    "favicon": "https://somafm.example/gs.png",
    "tags": "chillout, ambient ,LOFI, chillout",
    "countrycode": "US",
    "codec": "MP3",
    "bitrate": 128,
    "lastcheckok": 1
  },
  {
    "stationuuid": "def-456",
    "name": "Stale Station",
    "url": "https://dead.example/stream",
    "url_resolved": "",
    "tags": "",
    "bitrate": 0,
    "lastcheckok": 0
  }
])";
}

}  // namespace

TEST(RadioDirectoryParse, ReadsStationFields) {
    const QVector<DirectoryStation> stations = RadioDirectory::parseStations(sampleResponse());
    ASSERT_EQ(stations.size(), 2);

    const DirectoryStation& first = stations.at(0);
    EXPECT_EQ(first.uuid, QStringLiteral("abc-123"));
    EXPECT_EQ(first.name, QStringLiteral("Groove Salad"));
    EXPECT_EQ(first.countryCode, QStringLiteral("US"));
    EXPECT_EQ(first.codec, QStringLiteral("MP3"));
    EXPECT_EQ(first.bitrate, 128);
    EXPECT_TRUE(first.lastCheckOk);
}

TEST(RadioDirectoryParse, PrefersUrlResolvedButFallsBackToUrl) {
    const QVector<DirectoryStation> stations = RadioDirectory::parseStations(sampleResponse());
    ASSERT_EQ(stations.size(), 2);
    // url_resolved has already followed redirects and playlist indirection.
    EXPECT_EQ(stations.at(0).url, QStringLiteral("https://ice1.example/groovesalad-128-mp3"));
    // Regression: an empty url_resolved left the station with no URL at all
    // instead of falling back to the raw url.
    EXPECT_EQ(stations.at(1).url, QStringLiteral("https://dead.example/stream"));
}

TEST(RadioDirectoryParse, SplitsCommaSeparatedTagsAndDeduplicates) {
    const QVector<DirectoryStation> stations = RadioDirectory::parseStations(sampleResponse());
    ASSERT_FALSE(stations.isEmpty());
    // tags arrives as one comma-separated string, not an array, and casing is
    // normalised so it can be matched against the tag vocabulary.
    EXPECT_EQ(stations.at(0).tags, QStringList({QStringLiteral("chillout"),
                                               QStringLiteral("ambient"),
                                               QStringLiteral("lofi")}));
    EXPECT_TRUE(stations.at(1).tags.isEmpty());
}

TEST(RadioDirectoryParse, MapsLastCheckOkFromEitherNumberOrBool) {
    EXPECT_FALSE(RadioDirectory::parseStations(sampleResponse()).at(1).lastCheckOk);
    const QByteArray asBool =
        R"([{"name":"n","url":"https://a.example/s","lastcheckok":false}])";
    ASSERT_EQ(RadioDirectory::parseStations(asBool).size(), 1);
    EXPECT_FALSE(RadioDirectory::parseStations(asBool).first().lastCheckOk);
}

TEST(RadioDirectoryParse, DefaultsMissingFieldsAndSkipsUnusableRows) {
    const QByteArray sparse = R"([{"name":"Bare","url":"https://a.example/s"}])";
    const QVector<DirectoryStation> stations = RadioDirectory::parseStations(sparse);
    ASSERT_EQ(stations.size(), 1);
    EXPECT_EQ(stations.first().bitrate, 0);
    EXPECT_TRUE(stations.first().uuid.isEmpty());
    // Missing lastcheckok is optimistic rather than treated as broken.
    EXPECT_TRUE(stations.first().lastCheckOk);

    // A row with no name or no URL cannot be listed or played.
    EXPECT_TRUE(RadioDirectory::parseStations(R"([{"name":"No Url"}])").isEmpty());
    EXPECT_TRUE(RadioDirectory::parseStations(R"([{"url":"https://a.example/s"}])").isEmpty());
}

TEST(RadioDirectoryParse, MalformedJsonIsHandledGracefully) {
    EXPECT_TRUE(RadioDirectory::parseStations(QByteArray("not json at all")).isEmpty());
    EXPECT_TRUE(RadioDirectory::parseStations(QByteArray()).isEmpty());
    // An object rather than the expected array.
    EXPECT_TRUE(RadioDirectory::parseStations(QByteArray(R"({"error":"nope"})")).isEmpty());
    EXPECT_TRUE(RadioDirectory::parseStations(QByteArray("[1, 2, 3]")).isEmpty());
}

TEST(RadioDirectoryParse, ReadsTheTagVocabulary) {
    const QByteArray tags =
        R"([{"name":"jazz","stationcount":900},{"name":"LOFI","stationcount":400},{"name":"jazz"}])";
    EXPECT_EQ(RadioDirectory::parseTags(tags),
              QStringList({QStringLiteral("jazz"), QStringLiteral("lofi")}));
    EXPECT_TRUE(RadioDirectory::parseTags(QByteArray("garbage")).isEmpty());
}

TEST(RadioDirectoryMirrors, FallbackListIsUsableWithoutDns) {
    const QStringList mirrors = RadioDirectory::fallbackMirrors();
    ASSERT_FALSE(mirrors.isEmpty());
    const QRegularExpression host(QStringLiteral("^[a-z0-9.-]+\\.radio-browser\\.info$"));
    for (const QString& mirror : mirrors) {
        EXPECT_TRUE(host.match(mirror).hasMatch()) << mirror.toStdString();
        // A bare host is expected here; the scheme is added when requesting.
        EXPECT_FALSE(mirror.contains(QStringLiteral("://"))) << mirror.toStdString();
    }
}

TEST(RadioDirectoryQueries, RapidlyRetypedSearchesDoNotCrash) {
    // Regression: typing in the browse box crashed on the second character.
    // send() held `QNetworkReply*& slot` as a reference to the member, and
    // abort() emits finished() synchronously, so the finished handler nulled
    // that very member before the next line dereferenced it.
    RadioDirectory directory;
    for (int i = 0; i < 5; ++i) {
        directory.searchByName(QStringLiteral("jaz").left(1 + i % 3));
    }
    // The tag endpoint has its own in-flight slot and the same abort path.
    directory.fetchTags();
    directory.fetchTags();
    // Interleaving the two must not let one abort the other's reply.
    directory.searchByTag(QStringLiteral("lofi"));
    directory.topClicked();
    directory.topVoted();
    SUCCEED();
}

TEST(RadioPlayerSwitching, RapidStationSwitchesDoNotCrash) {
    // Switching stations aborts any in-flight playlist fetch, which is the same
    // synchronous-finished() reentrancy that crashed RadioDirectory.
    RadioPlayer player;
    for (int i = 0; i < 5; ++i) {
        RadioStation station;
        station.id = QStringLiteral("s%1").arg(i);
        station.name = QStringLiteral("Station %1").arg(i);
        // A playlist type, so play() starts a network fetch that the next
        // play() has to abort.
        station.url = QStringLiteral("https://198.51.100.7/stream%1.pls").arg(i);
        station.type = StreamType::Pls;
        player.play(station);
    }
    player.stop();
    EXPECT_EQ(player.state(), RadioState::Idle);
}
