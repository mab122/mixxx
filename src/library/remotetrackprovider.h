// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QString>
#include <functional>

/// Lets library features that mirror a remote library (e.g. Jellyfin) hand
/// out file paths before the file has been downloaded. Drag & drop and
/// loading by location accept such paths and ask the provider to download
/// the file first.
///
/// All functions must be called from the GUI thread.
class RemoteTrackProvider {
  public:
    virtual ~RemoteTrackProvider() = default;

    /// True if path belongs to this provider and can be downloaded.
    virtual bool canProvide(const QString& path) const = 0;

    /// Download path in the background. onDone(ok) is called on the GUI
    /// thread, synchronously if the file exists already.
    virtual void provide(const QString& path, std::function<void(bool ok)> onDone) = 0;

    static void registerProvider(RemoteTrackProvider* pProvider);
    static void unregisterProvider(RemoteTrackProvider* pProvider);

    /// The provider that can deliver the missing file at path, or nullptr if
    /// the file exists or nobody can provide it.
    static RemoteTrackProvider* forMissingFile(const QString& path);
};

/// Loads into a player that have to wait for a download.
///
/// When the download finishes the load is only executed if it is still the
/// most recent load request for that player: any other load into the player
/// (drag & drop, double-click, controller, another deferred load) in the
/// meantime cancels it.
namespace DeferredTrackLoad {

/// Registers a deferred load into group and returns its token.
quint64 begin(const QString& group);

/// True if the load identified by token is still wanted.
bool isStillWanted(const QString& group, quint64 token);

/// Called for every load into group; cancels pending deferred loads.
void invalidate(const QString& group);

} // namespace DeferredTrackLoad
