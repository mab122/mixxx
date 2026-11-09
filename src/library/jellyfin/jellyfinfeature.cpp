// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/jellyfinfeature.h"

#include <QAction>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QtDebug>

#include "analyzer/analyzerscheduledtrack.h"
#include "library/basetrackcache.h"
#include "library/jellyfin/dlgjellyfinlogin.h"
#include "library/jellyfin/jellyfincache.h"
#include "library/jellyfin/jellyfinclient.h"
#include "library/jellyfin/jellyfindao.h"
#include "library/jellyfin/jellyfinsettings.h"
#include "library/jellyfin/jellyfintrackmodel.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/crate.h"
#include "library/treeitem.h"
#include "library/treeitemmodel.h"
#include "moc_jellyfinfeature.cpp"
#include "track/track.h"
#include "widget/wlibrarysidebar.h"

namespace {

/// Ask before downloading more than this many tracks at once.
constexpr int kConfirmDownloadThreshold = 200;

} // namespace

JellyfinFeature::JellyfinFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseExternalLibraryFeature(pLibrary, pConfig, QStringLiteral("jellyfin")),
          m_database(pLibrary->trackCollectionManager()->internalCollection()->database()),
          m_pClient(new JellyfinClient(this)),
          m_pCache(new JellyfinCache(pConfig, m_pClient, this)),
          m_pTrackModel(nullptr),
          m_pPlaylistModel(nullptr),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_loginPromptShown(false),
          m_syncRunning(false) {
    // Tables must exist before the models create their views.
    if (!JellyfinDAO::createTables(m_database)) {
        qWarning() << "Jellyfin: failed to create shadow tables";
    }

    m_trackSource = QSharedPointer<BaseTrackCache>::create(
            m_pTrackCollection,
            JellyfinDAO::kLibraryTable,
            QStringLiteral("id"),
            JellyfinDAO::trackColumns(),
            JellyfinDAO::searchColumns(),
            false);

    m_pTrackModel = new JellyfinTrackModel(
            this, pLibrary->trackCollectionManager(), m_trackSource, m_pCache);
    m_pPlaylistModel = new JellyfinPlaylistModel(
            this, pLibrary->trackCollectionManager(), m_trackSource, m_pCache);
    m_pTrackModel->setSearch(QString()); // enables search

    connect(m_pCache, &JellyfinCache::activityChanged, this, [this]() {
        emit featureIsLoading(this, false); // refresh the download indicator
    });
    connect(m_pCache, &JellyfinCache::downloadFailed, this, &JellyfinFeature::slotDownloadFailed);

    // Show whatever was synced last time, even if the server is offline.
    reloadSidebar();

    // Refresh from the server in the background right after startup.
    if (JellyfinSettings(m_pConfig).isLoggedIn()) {
        QTimer::singleShot(0, this, &JellyfinFeature::startSync);
    }
}

JellyfinFeature::~JellyfinFeature() {
    m_pClient->cancel();
    delete m_pTrackModel;
    delete m_pPlaylistModel;
}

QVariant JellyfinFeature::title() {
    // e.g. "Jellyfin", "Jellyfin (offline)", "Jellyfin (↓ 2 · 45%)"
    QStringList parts;
    if (!m_status.isEmpty()) {
        parts.append(m_status);
    }
    const int pending = m_pCache->pendingCount();
    if (pending > 0) {
        const int percent = m_pCache->currentProgressPercent();
        parts.append(percent >= 0
                        ? QStringLiteral("↓ %1 · %2%").arg(pending).arg(percent)
                        : QStringLiteral("↓ %1").arg(pending));
    }
    if (parts.isEmpty()) {
        return tr("Jellyfin");
    }
    return tr("Jellyfin (%1)").arg(parts.join(QStringLiteral(" · ")));
}

void JellyfinFeature::setStatus(const QString& status) {
    m_status = status;
    emit featureIsLoading(this, false); // only renames the sidebar entry
}

void JellyfinFeature::slotDownloadFailed(const QString& label, const QString& error) {
    // Non-modal, and only one at a time.
    if (m_pErrorBox) {
        m_pErrorBox->setText(tr("Could not download “%1”:\n%2").arg(label, error));
        return;
    }
    m_pErrorBox = new QMessageBox(QMessageBox::Warning,
            tr("Jellyfin"),
            tr("Could not download “%1”:\n%2").arg(label, error),
            QMessageBox::Ok,
            dialogParent());
    m_pErrorBox->setAttribute(Qt::WA_DeleteOnClose);
    m_pErrorBox->setModal(false);
    m_pErrorBox->show();
}

void JellyfinFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    m_pSidebarWidget = pSidebarWidget;
    BaseExternalLibraryFeature::bindSidebarWidget(pSidebarWidget);
}

TreeItemModel* JellyfinFeature::sidebarModel() const {
    return m_pSidebarModel;
}

QWidget* JellyfinFeature::dialogParent() const {
    return m_pSidebarWidget ? m_pSidebarWidget->window() : nullptr;
}

QString JellyfinFeature::credentialsFingerprint() const {
    const JellyfinSettings settings(m_pConfig);
    return settings.serverUrl() + QChar('|') + settings.userId() + QChar('|') +
            settings.accessToken();
}

// ---------------------------------------------------------------------------

void JellyfinFeature::activate() {
    emit showTrackModel(m_pTrackModel);
    emit enableCoverArtDisplay(true);

    JellyfinSettings settings(m_pConfig);
    if (!settings.isLoggedIn()) {
        if (m_loginPromptShown) {
            return;
        }
        m_loginPromptShown = true;
        if (!promptLogin()) {
            return;
        }
    }
    // Sync once per session, and again whenever the account changed
    // (e.g. after logging in from the preferences).
    if (m_syncedFingerprint != credentialsFingerprint()) {
        startSync();
    }
}

void JellyfinFeature::activateChild(const QModelIndex& index) {
    const auto* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (!pItem) {
        return;
    }
    bool ok = false;
    const int playlistId = pItem->getData().toInt(&ok);
    if (!pItem->getData().isValid() || !ok) {
        // Category node (Playlists, Artists, ...): show all tracks.
        emit showTrackModel(m_pTrackModel);
        emit enableCoverArtDisplay(true);
        return;
    }
    m_pPlaylistModel->setPlaylistById(playlistId);
    emit showTrackModel(m_pPlaylistModel);
    emit enableCoverArtDisplay(true);
}

void JellyfinFeature::onRightClick(const QPoint& globalPos) {
    BaseExternalLibraryFeature::onRightClick(globalPos);
    const JellyfinSettings settings(m_pConfig);

    QMenu menu(m_pSidebarWidget);
    QAction* pSync = menu.addAction(tr("Sync with Server"));
    pSync->setEnabled(settings.isLoggedIn() && !m_syncRunning);
    menu.addSeparator();
    QAction* pDownloadAll = menu.addAction(tr("Download Whole Library for Offline Use"));
    QAction* pAnalyzeAll = menu.addAction(tr("Download and Analyze Whole Library"));
    pDownloadAll->setEnabled(settings.isLoggedIn());
    pAnalyzeAll->setEnabled(settings.isLoggedIn());
    const qint64 cacheBytes = JellyfinCache::diskUsage(m_pCache->cacheDir());
    QAction* pClearCache = menu.addAction(tr("Clear Download Cache (%1)…")
                    .arg(QLocale().formattedDataSize(cacheBytes)));
    pClearCache->setEnabled(cacheBytes > 0);
    menu.addSeparator();
    QAction* pLogin = nullptr;
    QAction* pLogout = nullptr;
    if (settings.isLoggedIn()) {
        pLogin = menu.addAction(tr("Log in with a Different Account…"));
        pLogout = menu.addAction(tr("Log out (%1 on %2)")
                        .arg(settings.userName(),
                                settings.serverName().isEmpty() ? settings.serverUrl()
                                                                : settings.serverName()));
    } else {
        pLogin = menu.addAction(tr("Log in…"));
    }

    QAction* pChosen = menu.exec(globalPos);
    if (!pChosen) {
        return;
    }
    if (pChosen == pSync) {
        startSync();
    } else if (pChosen == pDownloadAll) {
        runTrackAction(-1, tr("Jellyfin"), TrackAction::DownloadOnly);
    } else if (pChosen == pAnalyzeAll) {
        runTrackAction(-1, tr("Jellyfin"), TrackAction::Analyze);
    } else if (pChosen == pClearCache) {
        const auto answer = QMessageBox::question(dialogParent(),
                tr("Clear Jellyfin Download Cache"),
                tr("Delete all downloaded Jellyfin tracks (%1)?\n\nTracks loaded "
                   "in a deck are kept. Analysis, cues and loops stay in the "
                   "Mixxx library; tracks are downloaded again when needed.")
                        .arg(QLocale().formattedDataSize(cacheBytes)));
        if (answer == QMessageBox::Yes) {
            JellyfinCache::clear(m_pCache->cacheDir());
        }
    } else if (pChosen == pLogin) {
        promptLogin();
    } else if (pChosen == pLogout) {
        logout();
    }
}

void JellyfinFeature::onRightClickChild(const QPoint& globalPos, const QModelIndex& index) {
    const auto* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (!pItem || !pItem->getData().isValid()) {
        return; // category node
    }
    const int playlistId = pItem->getData().toInt();
    const QString name = pItem->getLabel();

    QMenu menu(m_pSidebarWidget);
    QAction* pDownload = menu.addAction(tr("Download for Offline Use"));
    QAction* pAnalyze = menu.addAction(tr("Download and Analyze"));
    QStringList paths;
    const QStringList fileNames = JellyfinDAO::fileNames(m_database, playlistId);
    for (const auto& fileName : fileNames) {
        paths.append(m_pCache->absolutePath(fileName));
    }
    const qint64 downloadedBytes = JellyfinCache::diskUsage(paths);
    QAction* pRemove = menu.addAction(tr("Remove Downloaded Files (%1)")
                    .arg(QLocale().formattedDataSize(downloadedBytes)));
    pRemove->setEnabled(downloadedBytes > 0);
    menu.addSeparator();
    QAction* pAutoDJBottom = menu.addAction(tr("Add to Auto DJ Queue (bottom)"));
    QAction* pAutoDJTop = menu.addAction(tr("Add to Auto DJ Queue (top)"));
    QAction* pAutoDJReplace = menu.addAction(tr("Add to Auto DJ Queue (replace)"));
    menu.addSeparator();
    QAction* pImportPlaylist = menu.addAction(tr("Import as Playlist"));
    QAction* pImportCrate = menu.addAction(tr("Import as Crate"));

    QAction* pChosen = menu.exec(globalPos);
    if (!pChosen) {
        return;
    }
    if (pChosen == pRemove) {
        // Explicit user action, no confirmation needed: files can be
        // downloaded again and the Mixxx library entries are kept.
        JellyfinCache::removeFiles(paths);
        return;
    }
    TrackAction action;
    if (pChosen == pDownload) {
        action = TrackAction::DownloadOnly;
    } else if (pChosen == pAnalyze) {
        action = TrackAction::Analyze;
    } else if (pChosen == pAutoDJBottom) {
        action = TrackAction::AutoDJBottom;
    } else if (pChosen == pAutoDJTop) {
        action = TrackAction::AutoDJTop;
    } else if (pChosen == pAutoDJReplace) {
        action = TrackAction::AutoDJReplace;
    } else if (pChosen == pImportPlaylist) {
        action = TrackAction::ImportAsPlaylist;
    } else if (pChosen == pImportCrate) {
        action = TrackAction::ImportAsCrate;
    } else {
        return;
    }
    runTrackAction(playlistId, name, action);
}

// ---------------------------------------------------------------------------
// Account

bool JellyfinFeature::promptLogin() {
    if (!DlgJellyfinLogin::login(m_pConfig, dialogParent())) {
        return false;
    }
    startSync();
    return true;
}

void JellyfinFeature::logout() {
    JellyfinSettings settings(m_pConfig);
    m_pClient->cancel();
    m_pClient->setCredentials(settings);
    m_pClient->logout(); // revoke the token on the server
    settings.clearLogin();
    m_pConfig->save();
    m_syncedFingerprint.clear();
    setStatus(QString());
}

// ---------------------------------------------------------------------------
// Sync

void JellyfinFeature::startSync() {
    const JellyfinSettings settings(m_pConfig);
    if (!settings.isLoggedIn() || m_syncRunning) {
        return;
    }
    m_syncRunning = true;
    const QString fingerprint = credentialsFingerprint();
    m_pClient->setCredentials(settings);
    setStatus(tr("syncing…"));

    m_pClient->fetchLibrary(
            [this](int done, int total) {
                setStatus(tr("syncing %1/%2").arg(done).arg(total));
            },
            [this, fingerprint](const JellyfinLibrarySnapshot& snapshot) {
                m_syncRunning = false;
                if (!JellyfinDAO::replaceAll(m_database, snapshot)) {
                    setStatus(QString());
                    QMessageBox::warning(dialogParent(),
                            tr("Jellyfin"),
                            tr("Failed to store the Jellyfin library in the "
                               "Mixxx database. See the log for details."));
                    return;
                }
                m_syncedFingerprint = fingerprint;
                qInfo() << "Jellyfin: synced" << snapshot.tracks.size() << "tracks,"
                        << snapshot.playlists.size() << "playlists";
                reloadSidebar();
                setStatus(QString());
            },
            [this](const QString& message, bool unauthorized) {
                m_syncRunning = false;
                qWarning() << "Jellyfin: sync failed:" << message;
                if (unauthorized) {
                    JellyfinSettings(m_pConfig).clearLogin();
                    m_pConfig->save();
                    setStatus(tr("logged out"));
                    const auto answer = QMessageBox::question(dialogParent(),
                            tr("Jellyfin"),
                            tr("Your Jellyfin session has expired or was revoked. "
                               "Log in again?"));
                    if (answer == QMessageBox::Yes) {
                        promptLogin();
                    }
                    return;
                }
                // Keep showing the last synced library; cached tracks remain
                // playable.
                setStatus(tr("offline"));
            });
}

void JellyfinFeature::reloadSidebar() {
    clearLastRightClickedIndex();
    auto pRoot = TreeItem::newRoot(this);
    JellyfinDAO::appendSidebarTree(m_database,
            pRoot.get(),
            {tr("Playlists"), tr("Artists"), tr("Albums"), tr("Genres")});
    m_pSidebarModel->setRootItem(std::move(pRoot));

    m_trackSource->buildIndex();
    m_pTrackModel->select();
    m_pPlaylistModel->select();
}

// ---------------------------------------------------------------------------
// Track actions

std::unique_ptr<BaseSqlTableModel> JellyfinFeature::createPlaylistModelForPlaylist(
        const QVariant& data) {
    bool ok = false;
    const int playlistId = data.toInt(&ok);
    if (!ok) {
        return {};
    }
    auto pModel = std::make_unique<JellyfinPlaylistModel>(
            this, m_pLibrary->trackCollectionManager(), m_trackSource, m_pCache);
    pModel->setPlaylistById(playlistId);
    return pModel;
}

void JellyfinFeature::appendTrackIdsFromRightClickIndex(
        QList<TrackId>* pTrackIds, QString* pPlaylist) {
    // Only used by the base class actions, which we replace. Never
    // download here: only return tracks that are already cached.
    const QModelIndex index = lastRightClickedIndex();
    const auto* pItem = index.isValid() ? static_cast<TreeItem*>(index.internalPointer())
                                        : nullptr;
    if (!pItem || !pItem->getData().isValid()) {
        return;
    }
    *pPlaylist = pItem->getLabel();
    QStringList paths;
    const QStringList fileNames = JellyfinDAO::fileNames(m_database, pItem->getData().toInt());
    for (const auto& fileName : fileNames) {
        const QString path = m_pCache->absolutePath(fileName);
        if (m_pCache->isCached(path)) {
            paths.append(path);
        }
    }
    pTrackIds->append(trackIdsForPaths(paths));
}

QList<TrackId> JellyfinFeature::trackIdsForPaths(const QStringList& paths) const {
    QList<TrackId> trackIds;
    trackIds.reserve(paths.size());
    for (const auto& path : paths) {
        const TrackPointer pTrack = m_pLibrary->trackCollectionManager()->getOrAddTrack(
                TrackRef::fromFilePath(path));
        if (pTrack && pTrack->getId().isValid()) {
            trackIds.append(pTrack->getId());
        }
    }
    return trackIds;
}

void JellyfinFeature::runTrackAction(int playlistId, const QString& name, TrackAction action) {
    QStringList paths;
    int missing = 0;
    const QStringList fileNames = JellyfinDAO::fileNames(m_database, playlistId);
    for (const auto& fileName : fileNames) {
        const QString path = m_pCache->absolutePath(fileName);
        if (!m_pCache->isCached(path)) {
            ++missing;
        }
        paths.append(path);
    }
    if (paths.isEmpty()) {
        return;
    }
    if (missing > 0 && !JellyfinSettings(m_pConfig).isLoggedIn()) {
        QMessageBox::information(dialogParent(),
                tr("Jellyfin"),
                tr("%n track(s) are not downloaded yet. Please log in to "
                   "Jellyfin first.",
                        "",
                        missing));
        return;
    }
    if (missing > kConfirmDownloadThreshold) {
        const auto answer = QMessageBox::question(dialogParent(),
                tr("Jellyfin"),
                tr("This will download %n track(s) from the server. The "
                   "cache size limit can be changed in the preferences. "
                   "Continue?",
                        "",
                        missing));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    m_pClient->setCredentials(JellyfinSettings(m_pConfig));

    QPointer<JellyfinFeature> pThis(this);
    m_pCache->downloadBatch(paths, [pThis, name, action](const QStringList& cachedPaths) {
        if (!pThis || action == TrackAction::DownloadOnly || cachedPaths.isEmpty()) {
            return;
        }
        const QList<TrackId> trackIds = pThis->trackIdsForPaths(cachedPaths);
        if (trackIds.isEmpty()) {
            return;
        }
        TrackCollection* pCollection = pThis->m_pTrackCollection;
        switch (action) {
        case TrackAction::Analyze: {
            QList<AnalyzerScheduledTrack> tracks;
            tracks.reserve(trackIds.size());
            for (const auto& trackId : trackIds) {
                tracks.append(AnalyzerScheduledTrack(trackId));
            }
            emit pThis->m_pLibrary->analyzeTracks(tracks);
            break;
        }
        case TrackAction::AutoDJBottom:
            pCollection->getPlaylistDAO().addTracksToAutoDJQueue(
                    trackIds, PlaylistDAO::AutoDJSendLoc::BOTTOM);
            break;
        case TrackAction::AutoDJTop:
            pCollection->getPlaylistDAO().addTracksToAutoDJQueue(
                    trackIds, PlaylistDAO::AutoDJSendLoc::TOP);
            break;
        case TrackAction::AutoDJReplace:
            pCollection->getPlaylistDAO().addTracksToAutoDJQueue(
                    trackIds, PlaylistDAO::AutoDJSendLoc::REPLACE);
            break;
        case TrackAction::ImportAsPlaylist: {
            QString playlistName = name;
            const int newPlaylistId =
                    pCollection->getPlaylistDAO().createUniquePlaylist(&playlistName);
            if (newPlaylistId != kInvalidPlaylistId) {
                pCollection->getPlaylistDAO().appendTracksToPlaylist(trackIds, newPlaylistId);
            }
            break;
        }
        case TrackAction::ImportAsCrate: {
            Crate crate;
            crate.setName(name);
            CrateId crateId;
            if (pCollection->insertCrate(crate, &crateId)) {
                pCollection->addCrateTracks(crateId, trackIds);
            } else {
                QMessageBox::warning(pThis->dialogParent(),
                        tr("Crate Creation Failed"),
                        tr("Could not create crate, it most likely already exists: ") +
                                name);
            }
            break;
        }
        case TrackAction::DownloadOnly:
            break;
        }
    });
}
