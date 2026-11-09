// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/jellyfincache.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QSaveFile>
#include <QTimer>
#include <QtDebug>
#include <utility>

#include "library/jellyfin/jellyfinclient.h"
#include "library/jellyfin/jellyfinsettings.h"
#include "mixer/playerinfo.h"
#include "moc_jellyfincache.cpp"

namespace {

constexpr int kMaxParallelDownloads = 2;
/// Selected-but-not-requested tracks kept in the queue.
constexpr int kMaxQueuedPrefetches = 3;

bool isLoadedInPlayer(const QString& path) {
    return PlayerInfo::instance().isFileLoaded(path);
}

} // namespace

JellyfinCache::JellyfinCache(
        UserSettingsPointer pConfig, JellyfinClient* pClient, QObject* parent)
        : QObject(parent),
          m_pConfig(std::move(pConfig)),
          m_pClient(pClient),
          m_nextBatchId(1) {
    RemoteTrackProvider::registerProvider(this);
}

JellyfinCache::~JellyfinCache() {
    RemoteTrackProvider::unregisterProvider(this);
    m_queue.clear();
    const QHash<QString, Job> active = std::exchange(m_active, {});
    m_activeOrder.clear();
    for (const auto& job : active) {
        if (job.pReply) {
            job.pReply->abort();
        }
    }
}

QString JellyfinCache::cacheDir() const {
    return JellyfinSettings(m_pConfig).cacheDir();
}

// static
QString JellyfinCache::fileNameFor(const QString& itemId, const QString& extension) {
    return itemId + QChar('.') + extension;
}

// static
QString JellyfinCache::itemIdFromPath(const QString& path) {
    return QFileInfo(path).completeBaseName();
}

QString JellyfinCache::absolutePath(const QString& fileName) const {
    return QDir(cacheDir()).filePath(fileName);
}

bool JellyfinCache::isCached(const QString& absolutePath) const {
    return !absolutePath.isEmpty() && QFileInfo::exists(absolutePath);
}

// static
void JellyfinCache::touch(const QString& absolutePath) {
    QFile file(absolutePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::ExistingOnly)) {
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
}

QNetworkReply* JellyfinCache::download(const QString& absolutePath,
        std::function<void(qint64, qint64)> onProgress,
        std::function<void(bool, const QString&)> onDone) {
    const QString itemId = itemIdFromPath(absolutePath);
    if (itemId.isEmpty()) {
        onDone(false, tr("Invalid cache path: %1").arg(absolutePath));
        return nullptr;
    }
    if (!QDir().mkpath(QFileInfo(absolutePath).absolutePath())) {
        onDone(false, tr("Cannot create cache directory %1").arg(cacheDir()));
        return nullptr;
    }
    auto pFile = std::make_shared<QSaveFile>(absolutePath);
    if (!pFile->open(QIODevice::WriteOnly)) {
        onDone(false, pFile->errorString());
        return nullptr;
    }

    QNetworkReply* pReply = m_pClient->startDownload(itemId);
    connect(pReply, &QNetworkReply::readyRead, this, [pReply, pFile]() {
        pFile->write(pReply->readAll());
    });
    if (onProgress) {
        connect(pReply, &QNetworkReply::downloadProgress, this, onProgress);
    }
    connect(pReply, &QNetworkReply::finished, this, [pReply, pFile, onDone]() {
        pReply->deleteLater();
        const int status = pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (pReply->error() != QNetworkReply::NoError || status != 200) {
            pFile->cancelWriting();
            pFile->commit();
            if (pReply->error() == QNetworkReply::OperationCanceledError) {
                onDone(false, QString());
            } else if (status == 401 || status == 403) {
                onDone(false, tr("Not authorized. Please log in to Jellyfin again."));
            } else {
                onDone(false, pReply->errorString());
            }
            return;
        }
        pFile->write(pReply->readAll());
        if (!pFile->commit()) {
            onDone(false, pFile->errorString());
            return;
        }
        onDone(true, QString());
    });
    return pReply;
}

bool JellyfinCache::canProvide(const QString& path) const {
    const QFileInfo fileInfo(path);
    return QDir(fileInfo.absolutePath()) == QDir(cacheDir()) &&
            !itemIdFromPath(path).isEmpty();
}

void JellyfinCache::provide(const QString& path, std::function<void(bool)> onDone) {
    fetch(path, QString(), Priority::High, std::move(onDone));
}

// ---------------------------------------------------------------------------
// Download queue

void JellyfinCache::fetch(const QString& absolutePath,
        const QString& label,
        Priority priority,
        DoneCallback onDone) {
    fetchImpl(absolutePath, label, priority, std::move(onDone), 0);
}

void JellyfinCache::fetchImpl(const QString& absolutePath,
        const QString& label,
        Priority priority,
        DoneCallback onDone,
        quint64 batchId) {
    if (absolutePath.isEmpty()) {
        if (onDone) {
            onDone(false);
        }
        return;
    }
    if (isCached(absolutePath)) {
        touch(absolutePath);
        if (onDone) {
            onDone(true);
        }
        return;
    }
    auto activeIt = m_active.find(absolutePath);
    if (activeIt != m_active.end()) {
        if (onDone) {
            activeIt->callbacks.append(std::move(onDone));
        }
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).path != absolutePath) {
            continue;
        }
        Job job = m_queue.takeAt(i);
        if (onDone) {
            job.callbacks.append(std::move(onDone));
        }
        if (priority == Priority::High) {
            job.priority = Priority::High;
            job.batchId = 0; // no longer cancelled with its batch
            m_queue.prepend(std::move(job));
        } else {
            m_queue.insert(i, std::move(job));
        }
        prunePrefetches();
        startJobs();
        emit activityChanged();
        return;
    }

    Job job;
    job.path = absolutePath;
    job.label = label.isEmpty() ? QFileInfo(absolutePath).fileName() : label;
    job.priority = priority;
    job.batchId = batchId;
    if (onDone) {
        job.callbacks.append(std::move(onDone));
    }
    if (priority == Priority::High) {
        m_queue.prepend(std::move(job));
    } else {
        m_queue.append(std::move(job));
    }
    prunePrefetches();
    startJobs();
    emit activityChanged();
}

void JellyfinCache::prunePrefetches() {
    // Keep only the most recent prefetches (selected tracks nobody waits for).
    int prefetches = 0;
    for (int i = 0; i < m_queue.size();) {
        const Job& job = m_queue.at(i);
        if (job.priority == Priority::High && job.callbacks.isEmpty() &&
                ++prefetches > kMaxQueuedPrefetches) {
            m_queue.removeAt(i);
            continue;
        }
        ++i;
    }
}

void JellyfinCache::startJobs() {
    while (m_active.size() < kMaxParallelDownloads && !m_queue.isEmpty()) {
        Job job = m_queue.takeFirst();
        const QString path = job.path;
        if (isCached(path)) {
            for (const auto& callback : std::as_const(job.callbacks)) {
                callback(true);
            }
            continue;
        }
        m_active.insert(path, std::move(job));
        m_activeOrder.append(path);
        QNetworkReply* pReply = download(
                path,
                [this, path](qint64 received, qint64 total) {
                    auto it = m_active.find(path);
                    if (it == m_active.end()) {
                        return;
                    }
                    const int oldPercent = it->total > 0
                            ? static_cast<int>(it->received * 100 / it->total)
                            : -1;
                    it->received = received;
                    it->total = total;
                    const int newPercent =
                            total > 0 ? static_cast<int>(received * 100 / total) : -1;
                    if (newPercent != oldPercent) {
                        emit activityChanged();
                    }
                },
                [this, path](bool ok, const QString& error) {
                    onJobFinished(path, ok, error);
                });
        auto it = m_active.find(path);
        if (it != m_active.end()) {
            it->pReply = pReply;
        }
    }
}

void JellyfinCache::onJobFinished(const QString& path, bool ok, const QString& error) {
    auto it = m_active.find(path);
    if (it == m_active.end()) {
        return; // destroyed or already handled
    }
    const Job job = std::move(*it);
    m_active.erase(it);
    m_activeOrder.removeAll(path);

    if (!ok && !error.isEmpty()) {
        qWarning() << "Jellyfin: download failed" << job.label << error;
        if (job.batchId == 0 && !job.callbacks.isEmpty()) {
            emit downloadFailed(job.label, error);
        }
    }
    for (const auto& callback : job.callbacks) {
        callback(ok);
    }
    emit activityChanged();
    // Not directly: we might be called synchronously from startJobs().
    QTimer::singleShot(0, this, &JellyfinCache::startJobs);
}

int JellyfinCache::currentProgressPercent() const {
    if (m_activeOrder.isEmpty()) {
        return -1;
    }
    const Job& job = m_active.value(m_activeOrder.first());
    if (job.total <= 0) {
        return -1;
    }
    return static_cast<int>(job.received * 100 / job.total);
}

void JellyfinCache::cancelBatch(quint64 batchId) {
    QList<Job> removed;
    for (int i = 0; i < m_queue.size();) {
        if (m_queue.at(i).batchId == batchId) {
            removed.append(m_queue.takeAt(i));
            continue;
        }
        ++i;
    }
    for (const auto& job : std::as_const(removed)) {
        for (const auto& callback : job.callbacks) {
            callback(false);
        }
    }
    emit activityChanged();
}

void JellyfinCache::downloadBatch(const QStringList& absolutePaths,
        std::function<void(const QStringList&)> onFinished) {
    struct BatchState {
        int total = 0;
        int remaining = 0;
        int failures = 0;
        bool cancelled = false;
        QStringList available;
        QPointer<QProgressDialog> pDialog;
        std::function<void(const QStringList&)> onFinished;
    };
    auto pState = std::make_shared<BatchState>();
    pState->onFinished = std::move(onFinished);

    QStringList pending;
    for (const auto& path : absolutePaths) {
        if (isCached(path)) {
            touch(path);
            pState->available.append(path);
        } else {
            pending.append(path);
        }
    }
    if (pending.isEmpty()) {
        if (pState->onFinished) {
            pState->onFinished(pState->available);
        }
        return;
    }
    pState->total = static_cast<int>(pending.size());
    pState->remaining = pState->total;

    const quint64 batchId = m_nextBatchId++;
    auto* pDialog = new QProgressDialog(
            tr("Downloading %n track(s) from Jellyfin…", "", pState->total),
            tr("Cancel"),
            0,
            pState->total,
            QApplication::activeWindow());
    pDialog->setWindowTitle(tr("Jellyfin"));
    pDialog->setWindowModality(Qt::NonModal);
    pDialog->setAttribute(Qt::WA_DeleteOnClose);
    pDialog->setMinimumDuration(0);
    pDialog->setAutoClose(false);
    pDialog->setAutoReset(false);
    pDialog->setValue(0);
    pState->pDialog = pDialog;
    connect(pDialog, &QProgressDialog::canceled, this, [this, pState, batchId]() {
        pState->cancelled = true;
        cancelBatch(batchId);
    });

    for (const auto& path : std::as_const(pending)) {
        fetchImpl(
                path,
                QString(),
                Priority::Low,
                [pState, path](bool ok) {
                    --pState->remaining;
                    if (ok) {
                        pState->available.append(path);
                    } else if (!pState->cancelled) {
                        ++pState->failures;
                    }
                    if (pState->pDialog) {
                        pState->pDialog->setValue(pState->total - pState->remaining);
                    }
                    if (pState->remaining > 0) {
                        return;
                    }
                    if (pState->pDialog) {
                        pState->pDialog->close();
                    }
                    if (pState->failures > 0) {
                        auto* pBox = new QMessageBox(QMessageBox::Warning,
                                tr("Jellyfin"),
                                tr("%n track(s) could not be downloaded. See the "
                                   "log for details.",
                                        "",
                                        pState->failures),
                                QMessageBox::Ok,
                                QApplication::activeWindow());
                        pBox->setAttribute(Qt::WA_DeleteOnClose);
                        pBox->setModal(false);
                        pBox->show();
                    }
                    if (pState->onFinished) {
                        pState->onFinished(pState->available);
                    }
                },
                batchId);
    }
}

// static
int JellyfinCache::removeFiles(const QStringList& absolutePaths) {
    int removed = 0;
    for (const auto& path : absolutePaths) {
        if (QFileInfo::exists(path) && !isLoadedInPlayer(path) && QFile::remove(path)) {
            ++removed;
        }
    }
    return removed;
}

// static
qint64 JellyfinCache::diskUsage(const QStringList& absolutePaths) {
    qint64 total = 0;
    for (const auto& path : absolutePaths) {
        total += QFileInfo(path).size(); // 0 if missing
    }
    return total;
}

// static
qint64 JellyfinCache::diskUsage(const QString& dir) {
    qint64 total = 0;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        total += entry.size();
    }
    return total;
}

// static
int JellyfinCache::clear(const QString& dir) {
    int removed = 0;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        const QString path = entry.absoluteFilePath();
        if (!isLoadedInPlayer(path) && QFile::remove(path)) {
            ++removed;
        }
    }
    return removed;
}
