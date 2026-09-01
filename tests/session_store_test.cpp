#include "core/Paths.h"
#include "core/SessionStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(SessionStore, RoundTrip) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("LOOM_STATE_DIR", dir.path().toUtf8());
    qputenv("LOOM_CONFIG_DIR", (dir.path() + QStringLiteral("/config")).toUtf8());

    Session session;
    session.active = 1;
    session.zoom = 120;
    session.zen = true;
    session.nextScratch = 4;
    session.geometry = QByteArray("geom");

    BufferState a;
    a.id = QStringLiteral("aaa-aaa");
    a.title = QStringLiteral("scratch-1");
    a.text = QStringLiteral("# hello\nworld");
    a.cursor = 3;
    a.scroll = 12;
    a.dirty = true;

    BufferState b;
    b.id = QStringLiteral("bbb-bbb");
    b.title = QStringLiteral("note.md");
    b.path = dir.path() + QStringLiteral("/note.md");
    b.text = QStringLiteral("named");
    b.dirty = false;
    session.buffers = {a, b};

    QString err;
    ASSERT_TRUE(SessionStore::save(session, &err)) << err.toStdString();

    const Session loaded = SessionStore::load();
    EXPECT_EQ(loaded.active, 1);
    EXPECT_EQ(loaded.zoom, 120);
    EXPECT_TRUE(loaded.zen);
    EXPECT_EQ(loaded.nextScratch, 4);
    ASSERT_EQ(loaded.buffers.size(), 2);
    EXPECT_EQ(loaded.buffers[0].title, QStringLiteral("scratch-1"));
    EXPECT_EQ(loaded.buffers[0].text, QStringLiteral("# hello\nworld"));
    EXPECT_TRUE(loaded.buffers[0].dirty);
    EXPECT_EQ(loaded.buffers[1].title, QStringLiteral("note.md"));
    EXPECT_EQ(SessionStore::readScratch(a.id), a.text);
}
