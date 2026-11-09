// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/jellyfindao.h"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>
#include <algorithm>

#include "library/jellyfin/jellyfincache.h"
#include "library/jellyfin/jellyfinclient.h"
#include "library/queryutil.h"
#include "library/treeitem.h"

namespace {

// Bump when the table layout changes; old tables are dropped and the
// content is re-fetched from the server on the next sync.
constexpr int kSchemaVersion = 3;

const QString kSchemaVersionTable = QStringLiteral("jellyfin_schema");

const QString kKindPlaylist = QStringLiteral("playlist");
const QString kKindArtist = QStringLiteral("artist");
const QString kKindAlbum = QStringLiteral("album");
const QString kKindGenre = QStringLiteral("genre");

bool exec(QSqlDatabase& db, const QString& sql) {
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    return true;
}

int storedSchemaVersion(QSqlDatabase& db) {
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT version FROM ") + kSchemaVersionTable) &&
            query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QString unknownArtist() {
    return QCoreApplication::translate("JellyfinDAO", "Unknown Artist");
}

QString unknownAlbum() {
    return QCoreApplication::translate("JellyfinDAO", "Unknown Album");
}

struct GroupedTrack {
    int rowId;
    const JellyfinTrackInfo* pInfo;
};

/// A virtual playlist derived from track metadata (artist, album, genre).
struct Group {
    QString name;
    QString parentKey; ///< album → artist key
    int year = 0;      ///< album: earliest year
    QList<GroupedTrack> tracks;
};

QString artistNameOf(const JellyfinTrackInfo& track) {
    if (!track.albumArtist.trimmed().isEmpty()) {
        return track.albumArtist.trimmed();
    }
    if (!track.artist.trimmed().isEmpty()) {
        return track.artist.trimmed();
    }
    return unknownArtist();
}

QString albumNameOf(const JellyfinTrackInfo& track) {
    const QString album = track.album.trimmed();
    return album.isEmpty() ? unknownAlbum() : album;
}

bool lessByDiscTrack(const GroupedTrack& a, const GroupedTrack& b) {
    if (a.pInfo->discNumber != b.pInfo->discNumber) {
        return a.pInfo->discNumber < b.pInfo->discNumber;
    }
    if (a.pInfo->trackNumber != b.pInfo->trackNumber) {
        return a.pInfo->trackNumber < b.pInfo->trackNumber;
    }
    return a.pInfo->title.compare(b.pInfo->title, Qt::CaseInsensitive) < 0;
}

bool lessByAlbum(const GroupedTrack& a, const GroupedTrack& b) {
    if (a.pInfo->year != b.pInfo->year) {
        return a.pInfo->year < b.pInfo->year;
    }
    const int cmp = albumNameOf(*a.pInfo).compare(albumNameOf(*b.pInfo), Qt::CaseInsensitive);
    if (cmp != 0) {
        return cmp < 0;
    }
    return lessByDiscTrack(a, b);
}

bool lessByArtistAlbum(const GroupedTrack& a, const GroupedTrack& b) {
    const int cmp = artistNameOf(*a.pInfo).compare(artistNameOf(*b.pInfo), Qt::CaseInsensitive);
    if (cmp != 0) {
        return cmp < 0;
    }
    return lessByAlbum(a, b);
}

class PlaylistWriter {
  public:
    explicit PlaylistWriter(QSqlDatabase& db)
            : m_insertPlaylist(db),
              m_insertTrack(db) {
        m_insertPlaylist.prepare(QStringLiteral(
                "INSERT INTO %1 (id, jellyfin_id, name, kind, parent_id, year)"
                " VALUES (:id, :jellyfin_id, :name, :kind, :parent_id, :year)")
                        .arg(JellyfinDAO::kPlaylistsTable));
        m_insertTrack.prepare(QStringLiteral(
                "INSERT INTO %1 (playlist_id, track_id, position)"
                " VALUES (:playlist_id, :track_id, :position)")
                        .arg(JellyfinDAO::kPlaylistTracksTable));
    }

    /// Returns the new playlist id or -1 on error.
    int addPlaylist(const QString& key,
            const QString& name,
            const QString& kind,
            const QList<int>& trackIds,
            int parentId = -1,
            int year = 0) {
        const int id = m_nextId++;
        m_insertPlaylist.bindValue(QStringLiteral(":id"), id);
        m_insertPlaylist.bindValue(QStringLiteral(":jellyfin_id"), key);
        m_insertPlaylist.bindValue(QStringLiteral(":name"), name);
        m_insertPlaylist.bindValue(QStringLiteral(":kind"), kind);
        m_insertPlaylist.bindValue(
                QStringLiteral(":parent_id"), parentId > 0 ? QVariant(parentId) : QVariant());
        m_insertPlaylist.bindValue(QStringLiteral(":year"), year > 0 ? QVariant(year) : QVariant());
        if (!m_insertPlaylist.exec()) {
            LOG_FAILED_QUERY(m_insertPlaylist);
            return -1;
        }
        int position = 1;
        for (const int trackId : trackIds) {
            m_insertTrack.bindValue(QStringLiteral(":playlist_id"), id);
            m_insertTrack.bindValue(QStringLiteral(":track_id"), trackId);
            m_insertTrack.bindValue(QStringLiteral(":position"), position++);
            if (!m_insertTrack.exec()) {
                LOG_FAILED_QUERY(m_insertTrack);
                return -1;
            }
        }
        return id;
    }

  private:
    QSqlQuery m_insertPlaylist;
    QSqlQuery m_insertTrack;
    int m_nextId = 1;
};

QList<int> sortedRowIds(QList<GroupedTrack> tracks,
        bool (*less)(const GroupedTrack&, const GroupedTrack&)) {
    std::stable_sort(tracks.begin(), tracks.end(), less);
    QList<int> ids;
    ids.reserve(tracks.size());
    for (const auto& track : std::as_const(tracks)) {
        ids.append(track.rowId);
    }
    return ids;
}

/// Groups sorted by display name, case-insensitively.
QList<QString> sortedKeys(const QHash<QString, Group>& groups) {
    QList<QString> keys = groups.keys();
    std::sort(keys.begin(), keys.end(), [&groups](const QString& a, const QString& b) {
        return groups.value(a).name.compare(groups.value(b).name, Qt::CaseInsensitive) < 0;
    });
    return keys;
}

} // namespace

namespace JellyfinDAO {

QStringList trackColumns() {
    return {
            QStringLiteral("id"),
            QStringLiteral("artist"),
            QStringLiteral("title"),
            QStringLiteral("album"),
            QStringLiteral("album_artist"),
            QStringLiteral("year"),
            QStringLiteral("genre"),
            QStringLiteral("tracknumber"),
            QStringLiteral("location"),
            QStringLiteral("duration"),
            QStringLiteral("bpm"),
            QStringLiteral("rating"),
    };
}

QStringList searchColumns() {
    return {
            QStringLiteral("artist"),
            QStringLiteral("album"),
            QStringLiteral("album_artist"),
            QStringLiteral("title"),
            QStringLiteral("genre"),
    };
}

bool createTables(QSqlDatabase& db) {
    if (storedSchemaVersion(db) != kSchemaVersion) {
        // Tables from an older version of this feature (or the earlier
        // FUSE based prototype): drop them, they only hold a copy of
        // server data.
        exec(db, QStringLiteral("DROP TABLE IF EXISTS ") + kPlaylistTracksTable);
        exec(db, QStringLiteral("DROP TABLE IF EXISTS ") + kPlaylistsTable);
        exec(db, QStringLiteral("DROP TABLE IF EXISTS ") + kLibraryTable);
        exec(db, QStringLiteral("DROP TABLE IF EXISTS ") + kSchemaVersionTable);
    }
    bool ok = exec(db,
                      QStringLiteral(
                              "CREATE TABLE IF NOT EXISTS %1 ("
                              " id INTEGER PRIMARY KEY,"
                              " jellyfin_id TEXT UNIQUE NOT NULL,"
                              " artist TEXT, title TEXT, album TEXT, album_artist TEXT,"
                              " year INTEGER, genre TEXT, tracknumber INTEGER,"
                              " location TEXT, duration INTEGER,"
                              " bpm INTEGER, rating INTEGER DEFAULT 0,"
                              " datetime_added TEXT)")
                              .arg(kLibraryTable)) &&
            // kind: playlist (from Jellyfin) or artist/album/genre (derived)
            exec(db,
                    QStringLiteral(
                            "CREATE TABLE IF NOT EXISTS %1 ("
                            " id INTEGER PRIMARY KEY,"
                            " jellyfin_id TEXT UNIQUE NOT NULL,"
                            " name TEXT,"
                            " kind TEXT NOT NULL,"
                            " parent_id INTEGER,"
                            " year INTEGER)")
                            .arg(kPlaylistsTable)) &&
            exec(db,
                    QStringLiteral(
                            "CREATE TABLE IF NOT EXISTS %1 ("
                            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            " playlist_id INTEGER REFERENCES %2(id),"
                            " track_id INTEGER REFERENCES %3(id),"
                            " position INTEGER)")
                            .arg(kPlaylistTracksTable, kPlaylistsTable, kLibraryTable)) &&
            exec(db,
                    QStringLiteral("CREATE INDEX IF NOT EXISTS %1_playlist_idx ON %1 (playlist_id)")
                            .arg(kPlaylistTracksTable)) &&
            exec(db,
                    QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (version INTEGER)")
                            .arg(kSchemaVersionTable));
    if (ok && storedSchemaVersion(db) != kSchemaVersion) {
        ok = exec(db, QStringLiteral("DELETE FROM ") + kSchemaVersionTable) &&
                exec(db,
                        QStringLiteral("INSERT INTO %1 (version) VALUES (%2)")
                                .arg(kSchemaVersionTable)
                                .arg(kSchemaVersion));
    }
    return ok;
}

bool replaceAll(QSqlDatabase& db, const JellyfinLibrarySnapshot& snapshot) {
    ScopedTransaction transaction(db);

    if (!exec(db, QStringLiteral("DELETE FROM ") + kPlaylistTracksTable) ||
            !exec(db, QStringLiteral("DELETE FROM ") + kPlaylistsTable) ||
            !exec(db, QStringLiteral("DELETE FROM ") + kLibraryTable)) {
        return false;
    }

    QSqlQuery insertTrack(db);
    insertTrack.prepare(QStringLiteral(
            "INSERT INTO %1 (id, jellyfin_id, artist, title, album, album_artist,"
            " year, genre, tracknumber, location, duration, datetime_added)"
            " VALUES (:id, :jellyfin_id, :artist, :title, :album, :album_artist,"
            " :year, :genre, :tracknumber, :location, :duration, :datetime_added)")
                    .arg(kLibraryTable));

    QHash<QString, int> rowIdByItemId;
    rowIdByItemId.reserve(snapshot.tracks.size());
    QHash<QString, Group> artists;
    QHash<QString, Group> albums;
    QHash<QString, Group> genres;

    int nextId = 1;
    for (const auto& track : snapshot.tracks) {
        if (track.id.isEmpty() || rowIdByItemId.contains(track.id)) {
            continue;
        }
        const int rowId = nextId++;
        insertTrack.bindValue(QStringLiteral(":id"), rowId);
        insertTrack.bindValue(QStringLiteral(":jellyfin_id"), track.id);
        insertTrack.bindValue(QStringLiteral(":artist"), track.artist);
        insertTrack.bindValue(QStringLiteral(":title"), track.title);
        insertTrack.bindValue(QStringLiteral(":album"), track.album);
        insertTrack.bindValue(QStringLiteral(":album_artist"), track.albumArtist);
        insertTrack.bindValue(QStringLiteral(":year"),
                track.year > 0 ? QVariant(track.year) : QVariant());
        insertTrack.bindValue(QStringLiteral(":genre"), track.genre);
        insertTrack.bindValue(QStringLiteral(":tracknumber"),
                track.trackNumber > 0 ? QVariant(track.trackNumber) : QVariant());
        insertTrack.bindValue(QStringLiteral(":location"),
                JellyfinCache::fileNameFor(track.id, track.extension));
        insertTrack.bindValue(QStringLiteral(":duration"),
                static_cast<int>(track.durationSecs + 0.5));
        insertTrack.bindValue(QStringLiteral(":datetime_added"), track.dateAdded);
        if (!insertTrack.exec()) {
            LOG_FAILED_QUERY(insertTrack);
            return false;
        }
        rowIdByItemId.insert(track.id, rowId);

        // Derived groups (case-insensitive, first spelling wins)
        const GroupedTrack grouped{rowId, &track};
        const QString artistName = artistNameOf(track);
        const QString artistKey = artistName.toLower();
        Group& artist = artists[artistKey];
        if (artist.name.isEmpty()) {
            artist.name = artistName;
        }
        artist.tracks.append(grouped);

        const QString albumName = albumNameOf(track);
        const QString albumKey = artistKey + QChar(0x1f) + albumName.toLower();
        Group& album = albums[albumKey];
        if (album.name.isEmpty()) {
            album.name = albumName;
            album.parentKey = artistKey;
        }
        if (track.year > 0 && (album.year == 0 || track.year < album.year)) {
            album.year = track.year;
        }
        album.tracks.append(grouped);

        const QStringList genreNames = track.genre.split(QStringLiteral(", "), Qt::SkipEmptyParts);
        for (const auto& genreName : genreNames) {
            Group& genre = genres[genreName.trimmed().toLower()];
            if (genre.name.isEmpty()) {
                genre.name = genreName.trimmed();
            }
            genre.tracks.append(grouped);
        }
    }

    PlaylistWriter writer(db);

    // Playlists from Jellyfin
    for (const auto& playlist : snapshot.playlists) {
        QList<int> trackIds;
        for (const auto& itemId : playlist.itemIds) {
            const int trackId = rowIdByItemId.value(itemId, -1);
            if (trackId >= 0) { // skip non-audio items
                trackIds.append(trackId);
            }
        }
        if (writer.addPlaylist(playlist.id, playlist.name, kKindPlaylist, trackIds) < 0) {
            return false;
        }
    }

    // Artists, then their albums
    QHash<QString, int> artistPlaylistIds;
    for (const auto& key : sortedKeys(artists)) {
        const Group& group = artists[key];
        const int id = writer.addPlaylist(QStringLiteral("artist:") + key,
                group.name,
                kKindArtist,
                sortedRowIds(group.tracks, lessByAlbum));
        if (id < 0) {
            return false;
        }
        artistPlaylistIds.insert(key, id);
    }
    for (const auto& key : sortedKeys(albums)) {
        const Group& group = albums[key];
        if (writer.addPlaylist(QStringLiteral("album:") + key,
                    group.name,
                    kKindAlbum,
                    sortedRowIds(group.tracks, lessByDiscTrack),
                    artistPlaylistIds.value(group.parentKey, -1),
                    group.year) < 0) {
            return false;
        }
    }
    for (const auto& key : sortedKeys(genres)) {
        const Group& group = genres[key];
        if (writer.addPlaylist(QStringLiteral("genre:") + key,
                    group.name,
                    kKindGenre,
                    sortedRowIds(group.tracks, lessByArtistAlbum)) < 0) {
            return false;
        }
    }

    transaction.commit();
    return true;
}

void appendSidebarTree(QSqlDatabase& db, TreeItem* pRoot, const SidebarLabels& labels) {
    TreeItem* pPlaylists = pRoot->appendChild(labels.playlists);
    TreeItem* pArtists = pRoot->appendChild(labels.artists);
    TreeItem* pAlbums = pRoot->appendChild(labels.albums);
    TreeItem* pGenres = pRoot->appendChild(labels.genres);

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
            "SELECT id, name, kind FROM %1 WHERE kind IN ('%2', '%3', '%4')"
            " ORDER BY name COLLATE NOCASE")
                    .arg(kPlaylistsTable, kKindPlaylist, kKindArtist, kKindGenre));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }
    QHash<int, TreeItem*> artistItems;
    while (query.next()) {
        const int id = query.value(0).toInt();
        const QString name = query.value(1).toString();
        const QString kind = query.value(2).toString();
        if (kind == kKindPlaylist) {
            pPlaylists->appendChild(name, id);
        } else if (kind == kKindArtist) {
            artistItems.insert(id, pArtists->appendChild(name, id));
        } else {
            pGenres->appendChild(name, id);
        }
    }

    // Albums below their artist (chronologically) ...
    query.prepare(QStringLiteral(
            "SELECT id, name, parent_id FROM %1 WHERE kind = '%2'"
            " ORDER BY parent_id, IFNULL(year, 9999), name COLLATE NOCASE")
                    .arg(kPlaylistsTable, kKindAlbum));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }
    while (query.next()) {
        TreeItem* pArtist = artistItems.value(query.value(2).toInt(), nullptr);
        if (pArtist) {
            pArtist->appendChild(query.value(1).toString(), query.value(0).toInt());
        }
    }

    // ... and all albums A–Z as "Album (Artist)"
    query.prepare(QStringLiteral(
            "SELECT a.id, a.name, p.name FROM %1 a LEFT JOIN %1 p ON p.id = a.parent_id"
            " WHERE a.kind = '%2' ORDER BY a.name COLLATE NOCASE, p.name COLLATE NOCASE")
                    .arg(kPlaylistsTable, kKindAlbum));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }
    while (query.next()) {
        pAlbums->appendChild(
                QStringLiteral("%1 (%2)").arg(query.value(1).toString(), query.value(2).toString()),
                query.value(0).toInt());
    }
}

QStringList fileNames(QSqlDatabase& db, int playlistId) {
    QSqlQuery query(db);
    if (playlistId < 0) {
        query.prepare(QStringLiteral("SELECT location FROM %1").arg(kLibraryTable));
    } else {
        query.prepare(QStringLiteral(
                "SELECT l.location FROM %1 pt JOIN %2 l ON l.id = pt.track_id"
                " WHERE pt.playlist_id = :playlist_id ORDER BY pt.position")
                        .arg(kPlaylistTracksTable, kLibraryTable));
        query.bindValue(QStringLiteral(":playlist_id"), playlistId);
    }
    QStringList result;
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return result;
    }
    while (query.next()) {
        result.append(query.value(0).toString());
    }
    return result;
}

int trackCount(QSqlDatabase& db) {
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM ") + kLibraryTable) && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

} // namespace JellyfinDAO
