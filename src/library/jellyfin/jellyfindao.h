// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class TreeItem;
struct JellyfinLibrarySnapshot;

/// Manages the jellyfin_* shadow tables inside mixxxdb.sqlite.
///
/// The tables persist between sessions so the library can be browsed (and
/// cached tracks played) while the server is offline. A successful sync
/// replaces their content atomically.
///
/// Besides the playlists of the server, virtual playlists are derived for
/// every artist, album and genre so they share the same models and
/// context menu actions.
///
/// The location column stores only the cache file name "<itemId>.<ext>";
/// the track models resolve it against the current cache directory.
namespace JellyfinDAO {

inline const QString kLibraryTable = QStringLiteral("jellyfin_library");
inline const QString kPlaylistsTable = QStringLiteral("jellyfin_playlists");
inline const QString kPlaylistTracksTable = QStringLiteral("jellyfin_playlist_tracks");

/// Columns exposed to BaseTrackCache (must exist in kLibraryTable).
QStringList trackColumns();
QStringList searchColumns();

/// Creates (or migrates) the shadow tables.
bool createTables(QSqlDatabase& db);

/// Replace all shadow table content with the snapshot. Returns false and
/// rolls back on error.
bool replaceAll(QSqlDatabase& db, const JellyfinLibrarySnapshot& snapshot);

struct SidebarLabels {
    QString playlists;
    QString artists;
    QString albums;
    QString genres;
};

/// Builds the sidebar below root:
///   Playlists / Artists → albums / Albums / Genres
/// Category nodes have no data; every other node's data is the id of a
/// (real or derived) playlist in kPlaylistsTable.
void appendSidebarTree(QSqlDatabase& db, TreeItem* pRoot, const SidebarLabels& labels);

/// Cache file names of all tracks, or of the given playlist (-1 = all).
QStringList fileNames(QSqlDatabase& db, int playlistId = -1);

int trackCount(QSqlDatabase& db);

} // namespace JellyfinDAO
