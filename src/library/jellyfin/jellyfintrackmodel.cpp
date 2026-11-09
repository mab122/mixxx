// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/jellyfintrackmodel.h"

#include <QPersistentModelIndex>
#include <QPointer>
#include <QUrl>

#include "library/jellyfin/jellyfincache.h"
#include "library/jellyfin/jellyfindao.h"
#include "library/trackcollectionmanager.h"
#include "moc_jellyfintrackmodel.cpp"
#include "track/track.h"

namespace {

/// Shared implementation of requestTrack() for both models.
template<typename Model>
void requestRemoteTrack(Model* pModel,
        JellyfinCache* pCache,
        TrackCollectionManager* pTrackCollectionManager,
        const QModelIndex& index,
        const QString& label,
        std::function<void(TrackPointer)> onReady) {
    const QString path = pModel->getTrackLocation(index);
    if (!pCache || path.isEmpty()) {
        if (onReady) {
            onReady(TrackPointer());
        }
        return;
    }
    if (!onReady) {
        pCache->fetch(path, label, JellyfinCache::Priority::High); // prefetch
        return;
    }
    QPointer<Model> pThis(pModel);
    const QPersistentModelIndex persistentIndex(index);
    pCache->fetch(path,
            label,
            JellyfinCache::Priority::High,
            [pThis, persistentIndex, path, pTrackCollectionManager, onReady](bool ok) {
                if (!ok) {
                    onReady(TrackPointer());
                    return;
                }
                // Prefer the row (applies the Jellyfin metadata to new
                // tracks); the view may have changed while downloading.
                if (pThis && persistentIndex.isValid() &&
                        pThis->getTrackLocation(persistentIndex) == path) {
                    onReady(pThis->getTrack(persistentIndex));
                    return;
                }
                onReady(pTrackCollectionManager->getOrAddTrack(TrackRef::fromFilePath(path)));
            });
}

template<typename Model>
QString rowLabel(const Model* pModel, const QModelIndex& index) {
    const QString artist = pModel->getFieldString(index, ColumnCache::COLUMN_LIBRARYTABLE_ARTIST);
    const QString title = pModel->getFieldString(index, ColumnCache::COLUMN_LIBRARYTABLE_TITLE);
    if (artist.isEmpty()) {
        return title;
    }
    return artist + QStringLiteral(" – ") + title;
}

} // namespace

JellyfinTrackModel::JellyfinTrackModel(QObject* parent,
        TrackCollectionManager* pTrackCollectionManager,
        QSharedPointer<BaseTrackCache> trackSource,
        JellyfinCache* pCache)
        : BaseExternalTrackModel(parent,
                  pTrackCollectionManager,
                  "mixxx.db.model.jellyfin",
                  JellyfinDAO::kLibraryTable,
                  std::move(trackSource)),
          m_pCache(pCache) {
}

QString JellyfinTrackModel::resolveLocation(const QString& nativeLocation) const {
    if (nativeLocation.isEmpty() || !m_pCache) {
        return QString();
    }
    return m_pCache->absolutePath(nativeLocation);
}

QString JellyfinTrackModel::label(const QModelIndex& index) const {
    return rowLabel(this, index);
}

TrackPointer JellyfinTrackModel::getTrack(const QModelIndex& index) const {
    // Never blocks: uncached tracks are fetched via requestTrack().
    const QString path = getTrackLocation(index);
    if (!m_pCache || !m_pCache->isCached(path)) {
        return TrackPointer();
    }
    JellyfinCache::touch(path);
    return BaseExternalTrackModel::getTrack(index);
}

bool JellyfinTrackModel::isTrackAvailable(const QModelIndex& index) const {
    return m_pCache && m_pCache->isCached(getTrackLocation(index));
}

void JellyfinTrackModel::requestTrack(const QModelIndex& index,
        std::function<void(TrackPointer)> onReady) {
    requestRemoteTrack(this,
            m_pCache.data(),
            m_pTrackCollectionManager,
            index,
            label(index),
            std::move(onReady));
}

TrackId JellyfinTrackModel::getTrackId(const QModelIndex& index) const {
    if (!m_pCache || !m_pCache->isCached(getTrackLocation(index))) {
        return TrackId();
    }
    return BaseExternalTrackModel::getTrackId(index);
}

QUrl JellyfinTrackModel::getTrackUrl(const QModelIndex& index) const {
    // Used for drag & drop, the receiver needs a real file. Uncached tracks
    // cannot be dragged; the download usually started when the row was
    // selected.
    if (!isTrackAvailable(index)) {
        return QUrl();
    }
    return BaseExternalTrackModel::getTrackUrl(index);
}

// ---------------------------------------------------------------------------

JellyfinPlaylistModel::JellyfinPlaylistModel(QObject* parent,
        TrackCollectionManager* pTrackCollectionManager,
        QSharedPointer<BaseTrackCache> trackSource,
        JellyfinCache* pCache)
        : BaseExternalPlaylistModel(parent,
                  pTrackCollectionManager,
                  "mixxx.db.model.jellyfin_playlist",
                  JellyfinDAO::kPlaylistsTable,
                  JellyfinDAO::kPlaylistTracksTable,
                  std::move(trackSource)),
          m_pCache(pCache) {
}

QString JellyfinPlaylistModel::resolveLocation(const QString& nativeLocation) const {
    if (nativeLocation.isEmpty() || !m_pCache) {
        return QString();
    }
    return m_pCache->absolutePath(nativeLocation);
}

QString JellyfinPlaylistModel::label(const QModelIndex& index) const {
    return rowLabel(this, index);
}

TrackPointer JellyfinPlaylistModel::getTrack(const QModelIndex& index) const {
    // Never blocks: uncached tracks are fetched via requestTrack().
    const QString path = getTrackLocation(index);
    if (!m_pCache || !m_pCache->isCached(path)) {
        return TrackPointer();
    }
    JellyfinCache::touch(path);
    return BaseExternalPlaylistModel::getTrack(index);
}

bool JellyfinPlaylistModel::isTrackAvailable(const QModelIndex& index) const {
    return m_pCache && m_pCache->isCached(getTrackLocation(index));
}

void JellyfinPlaylistModel::requestTrack(const QModelIndex& index,
        std::function<void(TrackPointer)> onReady) {
    requestRemoteTrack(this,
            m_pCache.data(),
            m_pTrackCollectionManager,
            index,
            label(index),
            std::move(onReady));
}

TrackId JellyfinPlaylistModel::getTrackId(const QModelIndex& index) const {
    if (!m_pCache || !m_pCache->isCached(getTrackLocation(index))) {
        return TrackId();
    }
    return BaseExternalPlaylistModel::getTrackId(index);
}

QUrl JellyfinPlaylistModel::getTrackUrl(const QModelIndex& index) const {
    // Used for drag & drop, the receiver needs a real file. Uncached tracks
    // cannot be dragged; the download usually started when the row was
    // selected.
    if (!isTrackAvailable(index)) {
        return QUrl();
    }
    return BaseExternalPlaylistModel::getTrackUrl(index);
}
