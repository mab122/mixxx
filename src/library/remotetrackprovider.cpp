// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/remotetrackprovider.h"

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

namespace {

QList<RemoteTrackProvider*>& providers() {
    static QList<RemoteTrackProvider*> s_providers;
    return s_providers;
}

/// group → token of the newest deferred load (0 = none)
QHash<QString, quint64>& pendingLoads() {
    static QHash<QString, quint64> s_pendingLoads;
    return s_pendingLoads;
}

quint64 s_nextToken = 1;
QMutex s_pendingLoadsMutex;

} // namespace

// static
void RemoteTrackProvider::registerProvider(RemoteTrackProvider* pProvider) {
    if (pProvider && !providers().contains(pProvider)) {
        providers().append(pProvider);
    }
}

// static
void RemoteTrackProvider::unregisterProvider(RemoteTrackProvider* pProvider) {
    providers().removeAll(pProvider);
}

// static
RemoteTrackProvider* RemoteTrackProvider::forMissingFile(const QString& path) {
    if (path.isEmpty() || providers().isEmpty() || QFileInfo::exists(path)) {
        return nullptr;
    }
    for (auto* pProvider : std::as_const(providers())) {
        if (pProvider->canProvide(path)) {
            return pProvider;
        }
    }
    return nullptr;
}

namespace DeferredTrackLoad {

quint64 begin(const QString& group) {
    const QMutexLocker locker(&s_pendingLoadsMutex);
    const quint64 token = s_nextToken++;
    pendingLoads().insert(group, token);
    return token;
}

bool isStillWanted(const QString& group, quint64 token) {
    const QMutexLocker locker(&s_pendingLoadsMutex);
    return pendingLoads().value(group, 0) == token;
}

void invalidate(const QString& group) {
    const QMutexLocker locker(&s_pendingLoadsMutex);
    pendingLoads().remove(group);
}

} // namespace DeferredTrackLoad
