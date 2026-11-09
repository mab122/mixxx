// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QPointer>
#include <QSharedPointer>

#include "library/baseexternalplaylistmodel.h"
#include "library/baseexternaltrackmodel.h"

class BaseTrackCache;
class JellyfinCache;
class TrackCollectionManager;

/// All tracks of the Jellyfin library.
///
/// Locations in the shadow table are cache file names. Tracks that are not
/// cached yet are not available (isTrackAvailable() == false): getTrack()
/// returns nullptr without blocking and requestTrack() downloads them in the
/// background (see WTrackTableView for the load/prefetch triggers).
class JellyfinTrackModel : public BaseExternalTrackModel {
    Q_OBJECT
  public:
    JellyfinTrackModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            QSharedPointer<BaseTrackCache> trackSource,
            JellyfinCache* pCache);

    TrackPointer getTrack(const QModelIndex& index) const override;
    TrackId getTrackId(const QModelIndex& index) const override;
    QUrl getTrackUrl(const QModelIndex& index) const override;
    bool isTrackAvailable(const QModelIndex& index) const override;
    void requestTrack(const QModelIndex& index,
            std::function<void(TrackPointer)> onReady) override;

  protected:
    QString resolveLocation(const QString& nativeLocation) const override;

  private:
    QString label(const QModelIndex& index) const;
    QPointer<JellyfinCache> m_pCache;
};

/// Tracks of one Jellyfin playlist. Same download semantics as above.
class JellyfinPlaylistModel : public BaseExternalPlaylistModel {
    Q_OBJECT
  public:
    JellyfinPlaylistModel(QObject* parent,
            TrackCollectionManager* pTrackCollectionManager,
            QSharedPointer<BaseTrackCache> trackSource,
            JellyfinCache* pCache);

    TrackPointer getTrack(const QModelIndex& index) const override;
    TrackId getTrackId(const QModelIndex& index) const override;
    QUrl getTrackUrl(const QModelIndex& index) const override;
    bool isTrackAvailable(const QModelIndex& index) const override;
    void requestTrack(const QModelIndex& index,
            std::function<void(TrackPointer)> onReady) override;

  protected:
    QString resolveLocation(const QString& nativeLocation) const override;

  private:
    QString label(const QModelIndex& index) const;
    QPointer<JellyfinCache> m_pCache;
};
