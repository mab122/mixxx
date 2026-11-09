// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QString>

#include "preferences/usersettings.h"

/// Typed accessors for the [Jellyfin] group in mixxx.cfg.
///
/// The password is never stored. After a successful login only the
/// server URL, the user id/name and the access token issued by the server
/// are persisted. The token can be revoked from the Jellyfin dashboard
/// (Dashboard → Devices) at any time.
class JellyfinSettings {
  public:
    explicit JellyfinSettings(UserSettingsPointer pConfig);

    QString serverUrl() const;
    void setServerUrl(const QString& url);

    QString serverName() const;
    void setServerName(const QString& name);

    QString userId() const;
    void setUserId(const QString& id);

    QString userName() const;
    void setUserName(const QString& name);

    QString accessToken() const;
    void setAccessToken(const QString& token);

    /// Stable per-installation device id sent to the server. Generated on
    /// first use.
    QString deviceId() const;

    /// Directory where downloaded audio files are kept.
    QString cacheDir() const;
    void setCacheDir(const QString& dir);
    static QString defaultCacheDir();

    /// True if we have a server and a token.
    bool isLoggedIn() const;

    /// Forget the token and user (keeps server URL for convenience).
    void clearLogin();

  private:
    UserSettingsPointer m_pConfig;
};
