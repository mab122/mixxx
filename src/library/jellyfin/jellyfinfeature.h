// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QList>
#include <QPointer>
#include <QSharedPointer>
#include <functional>

#include "library/baseexternallibraryfeature.h"
#include "track/trackid.h"
#include "util/parented_ptr.h"

class BaseTrackCache;
class JellyfinCache;
class JellyfinClient;
class JellyfinPlaylistModel;
class JellyfinTrackModel;
class QMessageBox;
class TreeItemModel;
class WLibrarySidebar;

/// "Jellyfin" entry in the library sidebar.
///
/// Browses the music library of a Jellyfin server. Track metadata is
/// synced into shadow tables of mixxxdb.sqlite; audio files are downloaded
/// into a local cache on demand (see JellyfinCache), so tracks can be
/// played and analyzed like any local file.
class JellyfinFeature : public BaseExternalLibraryFeature {
    Q_OBJECT
  public:
    JellyfinFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~JellyfinFeature() override;

    static bool isSupported() {
        return true;
    }

    QVariant title() override;
    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;
    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;

  private:
    enum class TrackAction {
        DownloadOnly,
        Analyze,
        AutoDJBottom,
        AutoDJTop,
        AutoDJReplace,
        ImportAsPlaylist,
        ImportAsCrate,
    };

    std::unique_ptr<BaseSqlTableModel> createPlaylistModelForPlaylist(
            const QVariant& data) override;
    void appendTrackIdsFromRightClickIndex(
            QList<TrackId>* pTrackIds, QString* pPlaylist) override;

    bool promptLogin();
    void logout();
    void startSync();
    void reloadSidebar();
    void setStatus(const QString& status);
    void slotDownloadFailed(const QString& label, const QString& error);
    QString credentialsFingerprint() const;

    /// Download the tracks of a playlist (-1 = whole library) and then run
    /// the action on their Mixxx track ids.
    void runTrackAction(int playlistId, const QString& name, TrackAction action);
    QList<TrackId> trackIdsForPaths(const QStringList& paths) const;

    QWidget* dialogParent() const;

    QSqlDatabase m_database;
    QSharedPointer<BaseTrackCache> m_trackSource;
    JellyfinClient* m_pClient;
    JellyfinCache* m_pCache;
    JellyfinTrackModel* m_pTrackModel;
    JellyfinPlaylistModel* m_pPlaylistModel;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    QPointer<WLibrarySidebar> m_pSidebarWidget;

    QString m_status; ///< sync state shown next to the title
    QPointer<QMessageBox> m_pErrorBox;
    QString m_syncedFingerprint;
    bool m_loginPromptShown;
    bool m_syncRunning;
};
