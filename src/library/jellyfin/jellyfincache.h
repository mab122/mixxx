// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

#include "library/remotetrackprovider.h"
#include "preferences/usersettings.h"

class JellyfinClient;
class QNetworkReply;

/// Local on-disk cache of Jellyfin audio files.
///
/// Mixxx can only decode and analyze local files, so every Jellyfin track
/// is mapped to a stable file name "<itemId>.<ext>" inside the cache
/// directory. Files are downloaded in the background, never blocking the
/// GUI. Because the path never changes, the Mixxx library entry of a track
/// including its analysis, cues and loops survives deleting and
/// re-downloading the file.
///
/// Nothing is ever deleted automatically: the user removes downloads
/// explicitly (sidebar context menu or preferences). Files loaded in a
/// player are always kept.
class JellyfinCache : public QObject, public RemoteTrackProvider {
    Q_OBJECT
  public:
    enum class Priority {
        /// The user is waiting for this track (load to deck, preview) or
        /// has selected it: goes to the front of the queue.
        High,
        /// Bulk download (whole playlist/album): goes to the back.
        Low,
    };

    using DoneCallback = std::function<void(bool ok)>;

    JellyfinCache(UserSettingsPointer pConfig, JellyfinClient* pClient, QObject* parent = nullptr);
    ~JellyfinCache() override;

    QString cacheDir() const;

    /// "<itemId>.<ext>" — what is stored in the shadow tables.
    static QString fileNameFor(const QString& itemId, const QString& extension);
    static QString itemIdFromPath(const QString& path);

    QString absolutePath(const QString& fileName) const;

    bool isCached(const QString& absolutePath) const;

    /// Marks a cached file as recently used.
    static void touch(const QString& absolutePath);

    /// Queues a download unless the file is cached already. onDone (may be
    /// empty) is called on the GUI thread when the file is available or the
    /// download failed — synchronously if the file is cached already.
    ///
    /// High priority requests without a callback are prefetches: only the
    /// most recent few are kept in the queue so that browsing the library
    /// does not pile up downloads.
    void fetch(const QString& absolutePath,
            const QString& label,
            Priority priority,
            DoneCallback onDone = DoneCallback());

    // RemoteTrackProvider: lets decks accept dropped paths of tracks that
    // are not downloaded yet.
    bool canProvide(const QString& path) const override;
    void provide(const QString& path, std::function<void(bool ok)> onDone) override;

    /// Downloads all missing files with low priority. A non-modal progress
    /// dialog allows cancelling. onFinished receives the paths that are
    /// available locally afterwards.
    void downloadBatch(const QStringList& absolutePaths,
            std::function<void(const QStringList& cachedPaths)> onFinished);

    /// Number of running + queued downloads.
    int pendingCount() const {
        return static_cast<int>(m_active.size() + m_queue.size());
    }
    /// Progress 0..100 of the oldest running download, -1 if unknown.
    int currentProgressPercent() const;

    static qint64 diskUsage(const QString& dir);
    /// Total size of those files that exist.
    static qint64 diskUsage(const QStringList& absolutePaths);
    /// Removes the given files except those loaded in a player. Returns the
    /// number of removed files.
    static int removeFiles(const QStringList& absolutePaths);
    /// Removes all files that are not loaded in a player. Returns the
    /// number of removed files.
    static int clear(const QString& dir);

  signals:
    /// The set of pending downloads or their progress changed.
    void activityChanged();
    /// A download the user was waiting for failed.
    void downloadFailed(const QString& label, const QString& error);

  private:
    struct Job {
        QString path;
        QString label;
        Priority priority = Priority::High;
        quint64 batchId = 0;
        QList<DoneCallback> callbacks;
        QPointer<QNetworkReply> pReply;
        qint64 received = 0;
        qint64 total = 0;
    };

    /// Starts downloading one file. onDone is always called exactly once,
    /// possibly synchronously if the download could not be started.
    QNetworkReply* download(const QString& absolutePath,
            std::function<void(qint64 received, qint64 total)> onProgress,
            std::function<void(bool ok, const QString& error)> onDone);

    void fetchImpl(const QString& absolutePath,
            const QString& label,
            Priority priority,
            DoneCallback onDone,
            quint64 batchId);
    void startJobs();
    void onJobFinished(const QString& path, bool ok, const QString& error);
    void prunePrefetches();
    void cancelBatch(quint64 batchId);

    UserSettingsPointer m_pConfig;
    JellyfinClient* m_pClient;

    QList<Job> m_queue;           ///< waiting, front = next
    QHash<QString, Job> m_active; ///< running, by path
    QList<QString> m_activeOrder; ///< running paths in start order
    quint64 m_nextBatchId;
};
