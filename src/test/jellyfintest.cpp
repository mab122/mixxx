// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include <gtest/gtest.h>

#include <QEventLoop>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>

#include "library/jellyfin/jellyfincache.h"
#include "library/jellyfin/jellyfinclient.h"
#include "library/jellyfin/jellyfindao.h"
#include "library/jellyfin/jellyfinsettings.h"
#include "library/remotetrackprovider.h"
#include "library/treeitem.h"
#include "mixer/playerinfo.h"
#include "test/librarytest.h" // includes test/mixxxdbtest.h
#include "util/dnd.h"

namespace {

class JellyfinTest : public MixxxDbTest {
  protected:
    JellyfinTest()
            : MixxxDbTest(true) {
        PlayerInfo::create();
    }
    ~JellyfinTest() override {
        PlayerInfo::destroy();
    }

    static JellyfinTrackInfo track(const QString& id, const QString& ext = QStringLiteral("mp3")) {
        JellyfinTrackInfo info;
        info.id = id;
        info.title = QStringLiteral("Title ") + id;
        info.artist = QStringLiteral("Artist");
        info.extension = ext;
        info.durationSecs = 61.6;
        return info;
    }

    /// Spin the event loop until done() is true or the timeout expires.
    static bool waitFor(const std::function<bool()>& done, int timeoutMs = 20000) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.start(timeoutMs);
        while (!done() && timeout.isActive()) {
            loop.processEvents(QEventLoop::AllEvents, 50);
        }
        return done();
    }
};

TEST_F(JellyfinTest, NormalizeServerUrl) {
    EXPECT_EQ(QStringLiteral("http://192.168.1.10:8096"),
            JellyfinClient::normalizeServerUrl(QStringLiteral(" 192.168.1.10:8096/ ")));
    EXPECT_EQ(QStringLiteral("https://jf.example.com/jellyfin"),
            JellyfinClient::normalizeServerUrl(
                    QStringLiteral("https://jf.example.com/jellyfin//")));
    EXPECT_EQ(QString(), JellyfinClient::normalizeServerUrl(QStringLiteral("   ")));
    EXPECT_EQ(QStringLiteral("http://jellyfin.jn48"),
            JellyfinClient::normalizeServerUrl(QStringLiteral("jellyfin.jn48")));
    EXPECT_EQ(QStringLiteral("http://jellyfin.jn48"),
            JellyfinClient::normalizeServerUrl(QStringLiteral("http://jellyfin.jn48")));
    EXPECT_EQ(QStringLiteral("HTTPS://jellyfin.jn48"),
            JellyfinClient::normalizeServerUrl(QStringLiteral("HTTPS://jellyfin.jn48/")));
    // host:port is not a scheme
    EXPECT_EQ(QStringLiteral("http://jellyfin.jn48:8096"),
            JellyfinClient::normalizeServerUrl(QStringLiteral("jellyfin.jn48:8096")));
    // a host that merely starts with "http"
    EXPECT_EQ(QStringLiteral("http://httpserver.lan"),
            JellyfinClient::normalizeServerUrl(QStringLiteral("httpserver.lan")));
}

TEST_F(JellyfinTest, CacheFileNames) {
    const QString fileName = JellyfinCache::fileNameFor(
            QStringLiteral("37974b3fe51f62ce0e0c60c83431f378"), QStringLiteral("flac"));
    EXPECT_EQ(QStringLiteral("37974b3fe51f62ce0e0c60c83431f378.flac"), fileName);
    EXPECT_EQ(QStringLiteral("37974b3fe51f62ce0e0c60c83431f378"),
            JellyfinCache::itemIdFromPath(QStringLiteral("/some/dir/") + fileName));
}

TEST_F(JellyfinTest, SettingsDropLegacyPassword) {
    config()->setValue(ConfigKey(QStringLiteral("[Jellyfin]"), QStringLiteral("Password")),
            QStringLiteral("secret"));
    JellyfinSettings settings(config());
    EXPECT_FALSE(config()->exists(
            ConfigKey(QStringLiteral("[Jellyfin]"), QStringLiteral("Password"))));
    EXPECT_FALSE(settings.isLoggedIn());
    // Device id is generated once and then stable.
    const QString deviceId = settings.deviceId();
    EXPECT_FALSE(deviceId.isEmpty());
    EXPECT_EQ(deviceId, JellyfinSettings(config()).deviceId());
}

TEST_F(JellyfinTest, DaoReplaceAll) {
    QSqlDatabase db = dbConnection();
    ASSERT_TRUE(JellyfinDAO::createTables(db));

    JellyfinLibrarySnapshot snapshot;
    snapshot.tracks = {track(QStringLiteral("a")),
            track(QStringLiteral("b"), QStringLiteral("flac")),
            track(QStringLiteral("c")),
            track(QStringLiteral("a"))}; // duplicate is ignored
    JellyfinPlaylistInfo playlist;
    playlist.id = QStringLiteral("p1");
    playlist.name = QStringLiteral("Friday Set");
    playlist.itemIds = {QStringLiteral("c"),
            QStringLiteral("unknown-video"),
            QStringLiteral("a")};
    snapshot.playlists = {playlist};

    ASSERT_TRUE(JellyfinDAO::replaceAll(db, snapshot));
    EXPECT_EQ(3, JellyfinDAO::trackCount(db));
    EXPECT_EQ(3, JellyfinDAO::fileNames(db).size());
    EXPECT_EQ((QStringList{QStringLiteral("c.mp3"), QStringLiteral("a.mp3")}),
            JellyfinDAO::fileNames(db, 1));

    QSqlQuery query(db);
    ASSERT_TRUE(query.exec(QStringLiteral(
            "SELECT duration, location FROM jellyfin_library WHERE jellyfin_id='b'")));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(62, query.value(0).toInt());
    EXPECT_EQ(QStringLiteral("b.flac"), query.value(1).toString());

    const JellyfinDAO::SidebarLabels labels{QStringLiteral("Playlists"),
            QStringLiteral("Artists"),
            QStringLiteral("Albums"),
            QStringLiteral("Genres")};
    auto pRoot = TreeItem::newRoot(nullptr);
    JellyfinDAO::appendSidebarTree(db, pRoot.get(), labels);
    ASSERT_EQ(4, pRoot->childRows());
    EXPECT_FALSE(pRoot->child(0)->getData().isValid()); // category node
    ASSERT_EQ(1, pRoot->child(0)->childRows());
    EXPECT_EQ(QStringLiteral("Friday Set"), pRoot->child(0)->child(0)->getLabel());

    // A second sync replaces everything.
    snapshot.tracks = {track(QStringLiteral("z"))};
    snapshot.playlists.clear();
    ASSERT_TRUE(JellyfinDAO::replaceAll(db, snapshot));
    EXPECT_EQ((QStringList{QStringLiteral("z.mp3")}), JellyfinDAO::fileNames(db));
    auto pRoot2 = TreeItem::newRoot(nullptr);
    JellyfinDAO::appendSidebarTree(db, pRoot2.get(), labels);
    EXPECT_EQ(0, pRoot2->child(0)->childRows()); // no playlists left

    // Creating the tables again keeps the content.
    ASSERT_TRUE(JellyfinDAO::createTables(db));
    EXPECT_EQ(1, JellyfinDAO::trackCount(db));
}

TEST_F(JellyfinTest, DaoGroupsArtistsAlbumsGenres) {
    QSqlDatabase db = dbConnection();
    ASSERT_TRUE(JellyfinDAO::createTables(db));

    auto make = [](const QString& id,
                        const QString& albumArtist,
                        const QString& album,
                        int year,
                        int disc,
                        int number,
                        const QString& genre) {
        JellyfinTrackInfo info = track(id);
        info.albumArtist = albumArtist;
        info.album = album;
        info.year = year;
        info.discNumber = disc;
        info.trackNumber = number;
        info.genre = genre;
        return info;
    };
    JellyfinLibrarySnapshot snapshot;
    snapshot.tracks = {
            make(QStringLiteral("hth2"),
                    QStringLiteral("AC/DC"),
                    QStringLiteral("Highway to Hell"),
                    1979,
                    1,
                    2,
                    QStringLiteral("Hard Rock")),
            make(QStringLiteral("bib1"),
                    QStringLiteral("AC/DC"),
                    QStringLiteral("Back in Black"),
                    1980,
                    1,
                    1,
                    QStringLiteral("Hard Rock, Rock")),
            make(QStringLiteral("hth1"),
                    QStringLiteral("ac/dc"),
                    QStringLiteral("highway to hell"),
                    1979,
                    1,
                    1,
                    QStringLiteral("hard rock")),
            make(QStringLiteral("x2"),
                    QStringLiteral("Zed"),
                    QString(),
                    0,
                    2,
                    1,
                    QString()),
            make(QStringLiteral("x1"),
                    QStringLiteral("Zed"),
                    QString(),
                    0,
                    1,
                    5,
                    QString()),
    };
    ASSERT_TRUE(JellyfinDAO::replaceAll(db, snapshot));

    const JellyfinDAO::SidebarLabels labels{QStringLiteral("Playlists"),
            QStringLiteral("Artists"),
            QStringLiteral("Albums"),
            QStringLiteral("Genres")};
    auto pRoot = TreeItem::newRoot(nullptr);
    JellyfinDAO::appendSidebarTree(db, pRoot.get(), labels);

    // Artists: case-insensitive, first spelling wins; albums chronological
    TreeItem* pArtists = pRoot->child(1);
    ASSERT_EQ(2, pArtists->childRows());
    TreeItem* pAcdc = pArtists->child(0);
    EXPECT_EQ(QStringLiteral("AC/DC"), pAcdc->getLabel());
    ASSERT_EQ(2, pAcdc->childRows());
    EXPECT_EQ(QStringLiteral("Highway to Hell"), pAcdc->child(0)->getLabel());
    EXPECT_EQ(QStringLiteral("Back in Black"), pAcdc->child(1)->getLabel());
    // Album tracks ordered by disc and track number
    EXPECT_EQ((QStringList{QStringLiteral("hth1.mp3"), QStringLiteral("hth2.mp3")}),
            JellyfinDAO::fileNames(db, pAcdc->child(0)->getData().toInt()));
    // Artist: albums chronologically
    EXPECT_EQ((QStringList{QStringLiteral("hth1.mp3"),
                      QStringLiteral("hth2.mp3"),
                      QStringLiteral("bib1.mp3")}),
            JellyfinDAO::fileNames(db, pAcdc->getData().toInt()));
    TreeItem* pZed = pArtists->child(1);
    ASSERT_EQ(1, pZed->childRows());
    EXPECT_EQ((QStringList{QStringLiteral("x1.mp3"), QStringLiteral("x2.mp3")}),
            JellyfinDAO::fileNames(db, pZed->child(0)->getData().toInt()));

    // Albums A–Z with artist
    TreeItem* pAlbums = pRoot->child(2);
    ASSERT_EQ(3, pAlbums->childRows());
    EXPECT_EQ(QStringLiteral("Back in Black (AC/DC)"), pAlbums->child(0)->getLabel());

    // Genres split on ", "
    TreeItem* pGenres = pRoot->child(3);
    ASSERT_EQ(2, pGenres->childRows());
    EXPECT_EQ(QStringLiteral("Hard Rock"), pGenres->child(0)->getLabel());
    EXPECT_EQ(3, JellyfinDAO::fileNames(db, pGenres->child(0)->getData().toInt()).size());
    EXPECT_EQ(QStringLiteral("Rock"), pGenres->child(1)->getLabel());
}

TEST_F(JellyfinTest, DaoMigratesPrototypeTables) {
    QSqlDatabase db = dbConnection();
    QSqlQuery query(db);
    // Layout of the earlier FUSE based prototype (no jellyfin_id column).
    ASSERT_TRUE(query.exec(QStringLiteral(
            "CREATE TABLE jellyfin_library (id INTEGER PRIMARY KEY, title TEXT)")));
    ASSERT_TRUE(query.exec(QStringLiteral(
            "INSERT INTO jellyfin_library (id, title) VALUES (1, 'old')")));
    ASSERT_TRUE(JellyfinDAO::createTables(db));
    EXPECT_EQ(0, JellyfinDAO::trackCount(db));
    JellyfinLibrarySnapshot snapshot;
    snapshot.tracks = {track(QStringLiteral("a"))};
    EXPECT_TRUE(JellyfinDAO::replaceAll(db, snapshot));
}

TEST_F(JellyfinTest, CacheRemoveFiles) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QByteArray data(1000, 'x');
    QStringList paths;
    for (const auto& name : {QStringLiteral("a.mp3"), QStringLiteral("b.mp3")}) {
        QFile file(dir.filePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(data);
        paths.append(file.fileName());
    }
    const QString missing = dir.filePath(QStringLiteral("missing.mp3"));
    EXPECT_EQ(2 * data.size(), JellyfinCache::diskUsage(paths + QStringList{missing}));
    EXPECT_EQ(1, JellyfinCache::removeFiles({paths.first(), missing}));
    EXPECT_FALSE(QFileInfo::exists(paths.first()));
    EXPECT_TRUE(QFileInfo::exists(paths.last()));
    EXPECT_EQ(data.size(), JellyfinCache::diskUsage(dir.path()));
    EXPECT_EQ(1, JellyfinCache::clear(dir.path()));
    EXPECT_EQ(0, JellyfinCache::diskUsage(dir.path()));
}

TEST_F(JellyfinTest, CacheQueueKeepsOnlyRecentPrefetches) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    JellyfinSettings(config()).setCacheDir(dir.path());
    JellyfinClient client;
    // Nothing listens there: downloads fail quickly.
    client.setCredentials(QStringLiteral("http://127.0.0.1:9"),
            QStringLiteral("user"),
            QStringLiteral("token"),
            QStringLiteral("device"));
    JellyfinCache cache(config(), &client);

    // Browsing: many selections, nobody waits for them.
    for (int i = 0; i < 20; ++i) {
        cache.fetch(dir.filePath(QStringLiteral("prefetch%1.mp3").arg(i)),
                QString(),
                JellyfinCache::Priority::High);
    }
    // 2 running + at most 3 queued
    EXPECT_LE(cache.pendingCount(), 5);

    // A track the user waits for is never dropped and reports failure.
    bool done = false;
    bool success = true;
    cache.fetch(dir.filePath(QStringLiteral("wanted.mp3")),
            QString(),
            JellyfinCache::Priority::High,
            [&](bool ok) {
                success = ok;
                done = true;
            });
    for (int i = 0; i < 20; ++i) {
        cache.fetch(dir.filePath(QStringLiteral("more%1.mp3").arg(i)),
                QString(),
                JellyfinCache::Priority::High);
    }
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_FALSE(success);
    ASSERT_TRUE(waitFor([&] { return cache.pendingCount() == 0; }));
}

TEST_F(JellyfinTest, DeferredLoadLatestRequestWins) {
    const QString deck1 = QStringLiteral("[Channel1]");
    const QString deck2 = QStringLiteral("[Channel2]");
    const quint64 first = DeferredTrackLoad::begin(deck1);
    const quint64 second = DeferredTrackLoad::begin(deck1);
    const quint64 other = DeferredTrackLoad::begin(deck2);
    EXPECT_FALSE(DeferredTrackLoad::isStillWanted(deck1, first));
    EXPECT_TRUE(DeferredTrackLoad::isStillWanted(deck1, second));
    // e.g. the user dropped a cached track onto the deck meanwhile
    DeferredTrackLoad::invalidate(deck1);
    EXPECT_FALSE(DeferredTrackLoad::isStillWanted(deck1, second));
    EXPECT_TRUE(DeferredTrackLoad::isStillWanted(deck2, other));
    DeferredTrackLoad::invalidate(deck2);
}

TEST_F(JellyfinTest, CacheProvidesMissingFilesOfItsDirectory) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    JellyfinSettings(config()).setCacheDir(dir.path());
    const QString missing = dir.filePath(QStringLiteral("37974b3fe51f62ce0e0c60c83431f378.mp3"));
    const QString existing = dir.filePath(QStringLiteral("cached.mp3"));
    {
        QFile file(existing);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
    }
    QTemporaryDir otherDir;
    const QString foreign = otherDir.filePath(QStringLiteral("foreign.mp3"));
    {
        JellyfinClient client;
        JellyfinCache cache(config(), &client);
        EXPECT_EQ(&cache, RemoteTrackProvider::forMissingFile(missing));
        EXPECT_EQ(nullptr, RemoteTrackProvider::forMissingFile(existing));
        EXPECT_EQ(nullptr, RemoteTrackProvider::forMissingFile(foreign));
    }
    // Unregistered on destruction
    EXPECT_EQ(nullptr, RemoteTrackProvider::forMissingFile(missing));
}

/// End-to-end test against a real server. Runs only if
/// MIXXX_JELLYFIN_TEST_URL, MIXXX_JELLYFIN_TEST_USER and
/// MIXXX_JELLYFIN_TEST_PASSWORD are set.
TEST_F(JellyfinTest, ServerLoginSyncDownload) {
    const QString url = qEnvironmentVariable("MIXXX_JELLYFIN_TEST_URL");
    if (url.isEmpty()) {
        GTEST_SKIP() << "MIXXX_JELLYFIN_TEST_URL not set";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    JellyfinSettings settings(config());
    settings.setCacheDir(dir.path());

    JellyfinClient client;
    client.setCredentials(url, QString(), QString(), settings.deviceId());

    // Server info
    bool done = false;
    QString error;
    JellyfinServerInfo info;
    client.fetchServerInfo(
            [&](const JellyfinServerInfo& i) {
                info = i;
                done = true;
            },
            [&](const QString& message, bool) {
                error = message;
                done = true;
            });
    ASSERT_TRUE(waitFor([&] { return done; }));
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    EXPECT_FALSE(info.version.isEmpty());

    // Wrong password
    done = false;
    bool unauthorized = false;
    client.authenticate(
            qEnvironmentVariable("MIXXX_JELLYFIN_TEST_USER"),
            QStringLiteral("definitely-wrong"),
            [&](const JellyfinAuthResult&) { done = true; },
            [&](const QString&, bool u) {
                unauthorized = u;
                done = true;
            });
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_TRUE(unauthorized);

    // Login
    done = false;
    JellyfinAuthResult auth;
    client.authenticate(
            qEnvironmentVariable("MIXXX_JELLYFIN_TEST_USER"),
            qEnvironmentVariable("MIXXX_JELLYFIN_TEST_PASSWORD"),
            [&](const JellyfinAuthResult& a) {
                auth = a;
                done = true;
            },
            [&](const QString& message, bool) {
                error = message;
                done = true;
            });
    ASSERT_TRUE(waitFor([&] { return done; }));
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    ASSERT_FALSE(auth.accessToken.isEmpty());
    settings.setServerUrl(url);
    settings.setUserId(auth.userId);
    settings.setAccessToken(auth.accessToken);
    client.setCredentials(settings);

    // Sync
    done = false;
    JellyfinLibrarySnapshot snapshot;
    client.fetchLibrary(
            nullptr,
            [&](const JellyfinLibrarySnapshot& s) {
                snapshot = s;
                done = true;
            },
            [&](const QString& message, bool) {
                error = message;
                done = true;
            });
    ASSERT_TRUE(waitFor([&] { return done; }));
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    ASSERT_FALSE(snapshot.tracks.isEmpty());
    for (const auto& t : std::as_const(snapshot.tracks)) {
        EXPECT_EQ(32, t.id.size());
        EXPECT_FALSE(t.extension.isEmpty());
        EXPECT_GT(t.durationSecs, 0.0);
    }
    QSqlDatabase db = dbConnection();
    ASSERT_TRUE(JellyfinDAO::createTables(db));
    ASSERT_TRUE(JellyfinDAO::replaceAll(db, snapshot));

    // Download every track into the cache
    JellyfinCache cache(config(), &client);
    QStringList paths;
    const QStringList fileNames = JellyfinDAO::fileNames(db);
    for (const auto& fileName : fileNames) {
        paths.append(cache.absolutePath(fileName));
    }
    done = false;
    QStringList cached;
    cache.downloadBatch(paths, [&](const QStringList& available) {
        cached = available;
        done = true;
    });
    ASSERT_TRUE(waitFor([&] { return done; }, 60000));
    EXPECT_EQ(paths.size(), cached.size());
    for (const auto& path : std::as_const(paths)) {
        EXPECT_TRUE(cache.isCached(path)) << path.toStdString();
        EXPECT_GT(QFileInfo(path).size(), 1000) << path.toStdString();
    }
    // Already cached → callback is called synchronously.
    bool cachedOk = false;
    cache.fetch(paths.first(), QString(), JellyfinCache::Priority::High, [&](bool ok) {
        cachedOk = ok;
    });
    EXPECT_TRUE(cachedOk);
    EXPECT_EQ(0, cache.pendingCount());

    // Revoked token → unauthorized
    client.logout();
    waitFor([] { return false; }, 1000);
    done = false;
    unauthorized = false;
    client.fetchLibrary(
            nullptr,
            [&](const JellyfinLibrarySnapshot&) { done = true; },
            [&](const QString&, bool u) {
                unauthorized = u;
                done = true;
            });
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_TRUE(unauthorized);
}

} // namespace

// Drag & drop needs registered sound sources (to accept the file type).
namespace {

class JellyfinDndTest : public LibraryTest {};

TEST_F(JellyfinDndTest, DropTargetsAcceptNotYetDownloadedTracks) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    JellyfinSettings(config()).setCacheDir(dir.path());
    const QString missing = dir.filePath(QStringLiteral("37974b3fe51f62ce0e0c60c83431f378.flac"));
    QTemporaryDir otherDir;
    const QString foreign = otherDir.filePath(QStringLiteral("foreign.flac"));
    const QList<QUrl> urls{QUrl::fromLocalFile(missing)};

    // Without a provider a missing file is rejected.
    EXPECT_TRUE(DragAndDropHelper::supportedTracksFromUrls(urls, true, false).isEmpty());

    JellyfinClient client;
    JellyfinCache cache(config(), &client);
    const auto files = DragAndDropHelper::supportedTracksFromUrls(urls, true, false);
    ASSERT_EQ(1, files.size());
    EXPECT_EQ(missing, files.first().location());
    EXPECT_TRUE(DragAndDropHelper::supportedTracksFromUrls(
            {QUrl::fromLocalFile(foreign)}, true, false)
                    .isEmpty());
}

} // namespace
