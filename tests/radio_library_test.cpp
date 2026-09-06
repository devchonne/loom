#include "core/StationLibrary.h"

#include "core/Paths.h"
#include "core/PlaylistResolver.h"
#include "core/RadioStation.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

RadioStation makeStation(const QString& id, const QString& name, const QStringList& genre = {}) {
    RadioStation station;
    station.id = id;
    station.name = name;
    station.url = QStringLiteral("https://example.com/%1").arg(id);
    station.genre = genre;
    station.addedAt = QDateTime::currentDateTimeUtc();
    return station;
}

}  // namespace

TEST(InferStreamType, ReadsTheExtension) {
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/stream")), StreamType::Direct);
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/x.mp3")), StreamType::Direct);
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/x.pls")), StreamType::Pls);
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/x.m3u")), StreamType::M3u);
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/x.m3u8")), StreamType::M3u8);
}

TEST(InferStreamType, IsCaseInsensitiveAndIgnoresQueryStrings) {
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/X.PLS")), StreamType::Pls);
    // Regression: a query string carrying its own dots was read as the
    // extension, so a direct stream came back as a playlist.
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/listen?fmt=x.pls&id=2")),
              StreamType::Direct);
    EXPECT_EQ(inferStreamType(QStringLiteral("https://a.example/x.pls?token=abc")), StreamType::Pls);
}

TEST(InferStreamType, HandlesEmptyAndPaddedInput) {
    EXPECT_EQ(inferStreamType(QString()), StreamType::Direct);
    EXPECT_EQ(inferStreamType(QStringLiteral("  https://a.example/x.m3u  ")), StreamType::M3u);
}

TEST(PlaylistResolver, ParsesPls) {
    const QByteArray pls =
        "[playlist]\r\nNumberOfEntries=2\r\nFile1=https://a.example/one\r\nFile2=https://a.example/"
        "two\r\n";
    EXPECT_EQ(PlaylistResolver::parsePls(pls), QStringLiteral("https://a.example/one"));
}

TEST(PlaylistResolver, PrefersTheLowestNumberedEntry) {
    // Regression: a .pls listing File2 before File1 handed back the second
    // stream, which is not the one the station advertises.
    const QByteArray pls = "[playlist]\nFile2=https://a.example/two\nFile1=https://a.example/one\n";
    EXPECT_EQ(PlaylistResolver::parsePls(pls), QStringLiteral("https://a.example/one"));
}

TEST(PlaylistResolver, ParsesM3uSkippingComments) {
    const QByteArray m3u = "#EXTM3U\r\n#EXTINF:-1,Some Station\r\nhttps://a.example/stream\r\n";
    EXPECT_EQ(PlaylistResolver::parseM3u(m3u), QStringLiteral("https://a.example/stream"));
}

TEST(PlaylistResolver, ReturnsEmptyForUnusableInput) {
    EXPECT_TRUE(PlaylistResolver::parsePls(QByteArray("[playlist]\nNumberOfEntries=0\n")).isEmpty());
    EXPECT_TRUE(PlaylistResolver::parseM3u(QByteArray("#EXTM3U\n#EXTINF:-1,nothing\n")).isEmpty());
    EXPECT_TRUE(PlaylistResolver::parseM3u(QByteArray()).isEmpty());
}

TEST(PlaylistResolver, DispatchesOnType) {
    const QByteArray m3u = "https://a.example/stream\n";
    EXPECT_EQ(PlaylistResolver::firstStreamUrl(StreamType::M3u, m3u),
              QStringLiteral("https://a.example/stream"));
    // A direct stream needs no resolution at all.
    EXPECT_TRUE(PlaylistResolver::firstStreamUrl(StreamType::Direct, m3u).isEmpty());
}

TEST(StationLibrary, RoundTripsThroughDisk) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());
    qputenv("LOOM_CONFIG_DIR", (dir.path() + QStringLiteral("/config")).toUtf8());

    StationLibrary library;
    RadioStation station = makeStation(QString(), QStringLiteral("Groove Salad"),
                                       {QStringLiteral("chillout"), QStringLiteral("lofi")});
    station.url = QStringLiteral("https://ice.example/groovesalad.pls");
    station.source = StationSource::Crawler;
    station.favicon = QStringLiteral("https://ice.example/favicon.ico");
    const QString id = library.add(station);
    ASSERT_FALSE(id.isEmpty());
    ASSERT_TRUE(library.toggleFavorite(id));

    StationLibrary reloaded;
    reloaded.load();
    ASSERT_EQ(reloaded.count(), 1);
    const RadioStation* found = reloaded.byId(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, QStringLiteral("Groove Salad"));
    // The type is inferred on add rather than trusted from the caller.
    EXPECT_EQ(found->type, StreamType::Pls);
    EXPECT_EQ(found->source, StationSource::Crawler);
    EXPECT_EQ(found->favicon, QStringLiteral("https://ice.example/favicon.ico"));
    EXPECT_TRUE(found->isFavorite);
    EXPECT_TRUE(found->addedAt.isValid());
    EXPECT_FALSE(found->lastPlayedAt.isValid());
    EXPECT_EQ(found->genre.size(), 2);
}

TEST(StationLibrary, MarkPlayedStampsTheStation) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());

    StationLibrary library;
    const QString id = library.add(makeStation(QString(), QStringLiteral("Jazz")));
    ASSERT_FALSE(library.byId(id)->lastPlayedAt.isValid());
    library.markPlayed(id);
    EXPECT_TRUE(library.byId(id)->lastPlayedAt.isValid());
}

TEST(StationLibrary, RemoveDropsTheEntry) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());

    StationLibrary library;
    const QString id = library.add(makeStation(QString(), QStringLiteral("Jazz")));
    EXPECT_TRUE(library.remove(id));
    EXPECT_TRUE(library.isEmpty());
    EXPECT_FALSE(library.remove(id));
}

TEST(StationLibrary, MissingFileYieldsAnEmptyLibrary) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());

    StationLibrary library;
    library.load();
    EXPECT_TRUE(library.isEmpty());
}

TEST(StationLibrary, CorruptOrPartialDataIsDiscardedNotCrashed) {
    EXPECT_TRUE(StationLibrary::deserialize(QByteArray("this is not json")).isEmpty());
    EXPECT_TRUE(StationLibrary::deserialize(QByteArray("[]")).isEmpty());
    EXPECT_TRUE(StationLibrary::deserialize(QByteArray()).isEmpty());
    // Regression: an entry with no id or url cannot be played or addressed, so
    // it is dropped rather than kept as a broken row.
    const QByteArray partial =
        R"({"version":1,"stations":[{"name":"no url"},{"id":"x","url":"https://a.example/s","name":"ok"}]})";
    const QVector<RadioStation> parsed = StationLibrary::deserialize(partial);
    ASSERT_EQ(parsed.size(), 1);
    EXPECT_EQ(parsed.first().name, QStringLiteral("ok"));
}

TEST(StationLibraryFilter, AllViewKeepsEverything) {
    const QVector<RadioStation> stations{makeStation(QStringLiteral("a"), QStringLiteral("Alpha")),
                                         makeStation(QStringLiteral("b"), QStringLiteral("Beta"))};
    EXPECT_EQ(StationLibrary::filter(stations, StationLibrary::View::All, QString(), QString()).size(),
              2);
}

TEST(StationLibraryFilter, FavoritesViewKeepsOnlyFlaggedStations) {
    QVector<RadioStation> stations{makeStation(QStringLiteral("a"), QStringLiteral("Alpha")),
                                   makeStation(QStringLiteral("b"), QStringLiteral("Beta"))};
    stations[1].isFavorite = true;
    const QVector<RadioStation> out =
        StationLibrary::filter(stations, StationLibrary::View::Favorites, QString(), QString());
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out.first().name, QStringLiteral("Beta"));
}

TEST(StationLibraryFilter, RecentViewDropsNeverPlayedAndSortsNewestFirst) {
    QVector<RadioStation> stations{makeStation(QStringLiteral("a"), QStringLiteral("Alpha")),
                                   makeStation(QStringLiteral("b"), QStringLiteral("Beta")),
                                   makeStation(QStringLiteral("c"), QStringLiteral("Gamma"))};
    stations[0].lastPlayedAt = QDateTime::currentDateTimeUtc().addSecs(-600);
    stations[2].lastPlayedAt = QDateTime::currentDateTimeUtc();
    const QVector<RadioStation> out =
        StationLibrary::filter(stations, StationLibrary::View::Recent, QString(), QString());
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out.at(0).name, QStringLiteral("Gamma"));
    EXPECT_EQ(out.at(1).name, QStringLiteral("Alpha"));
}

TEST(StationLibraryFilter, QueryMatchesNameAndTagsAcrossTokens) {
    const QVector<RadioStation> stations{
        makeStation(QStringLiteral("a"), QStringLiteral("Groove Salad"),
                    {QStringLiteral("chillout")}),
        makeStation(QStringLiteral("b"), QStringLiteral("Deep House Radio"),
                    {QStringLiteral("house")})};

    EXPECT_EQ(
        StationLibrary::filter(stations, StationLibrary::View::All, QStringLiteral("groove"), {})
            .size(),
        1);
    // Tags are searched alongside the name.
    EXPECT_EQ(
        StationLibrary::filter(stations, StationLibrary::View::All, QStringLiteral("chillout"), {})
            .size(),
        1);
    // Tokens are ANDed, so both have to hit the same station.
    EXPECT_TRUE(
        StationLibrary::filter(stations, StationLibrary::View::All, QStringLiteral("groove house"), {})
            .isEmpty());
    EXPECT_EQ(StationLibrary::filter(stations, StationLibrary::View::All,
                                     QStringLiteral("salad chillout"), {})
                  .size(),
              1);
}

TEST(StationLibraryFilter, GenreNarrowsByExactTag) {
    const QVector<RadioStation> stations{
        makeStation(QStringLiteral("a"), QStringLiteral("Alpha"), {QStringLiteral("jazz")}),
        makeStation(QStringLiteral("b"), QStringLiteral("Beta"), {QStringLiteral("lofi")})};
    const QVector<RadioStation> out = StationLibrary::filter(stations, StationLibrary::View::All,
                                                            QString(), QStringLiteral("jazz"));
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out.first().name, QStringLiteral("Alpha"));
    EXPECT_TRUE(StationLibrary::filter(stations, StationLibrary::View::All, QString(),
                                       QStringLiteral("metal"))
                    .isEmpty());
}

TEST(StationLibrary, GenresAreDeduplicatedAndSorted) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());

    StationLibrary library;
    library.add(makeStation(QString(), QStringLiteral("Alpha"),
                            {QStringLiteral("lofi"), QStringLiteral("jazz")}));
    library.add(makeStation(QString(), QStringLiteral("Beta"),
                            {QStringLiteral("Jazz"), QStringLiteral("ambient")}));
    // "jazz" appears twice in different cases and must collapse to one entry.
    EXPECT_EQ(library.genres(),
              QStringList({QStringLiteral("ambient"), QStringLiteral("jazz"),
                           QStringLiteral("lofi")}));
}

TEST(StationLibrary, FindByNamePrefersAnExactMatch) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());

    StationLibrary library;
    library.add(makeStation(QString(), QStringLiteral("Jazz Radio Classics")));
    library.add(makeStation(QString(), QStringLiteral("Jazz")));

    // Regression: `/radio play jazz` matched the longer name first because the
    // substring scan ran before the exact-name scan.
    const RadioStation* found = library.findByName(QStringLiteral("jazz"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, QStringLiteral("Jazz"));

    const RadioStation* partial = library.findByName(QStringLiteral("classics"));
    ASSERT_NE(partial, nullptr);
    EXPECT_EQ(partial->name, QStringLiteral("Jazz Radio Classics"));

    EXPECT_EQ(library.findByName(QStringLiteral("nothing here")), nullptr);
    EXPECT_EQ(library.findByName(QString()), nullptr);
}
