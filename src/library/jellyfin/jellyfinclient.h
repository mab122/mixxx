// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrlQuery>
#include <functional>

class QNetworkReply;
class JellyfinSettings;

struct JellyfinTrackInfo {
    QString id; ///< Jellyfin item id (32 hex chars)
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    QString extension; ///< lowercase file suffix without dot, e.g. "flac"
    QString dateAdded; ///< ISO 8601
    int year = 0;
    int trackNumber = 0;
    int discNumber = 0;
    double durationSecs = 0.0;
};

struct JellyfinPlaylistInfo {
    QString id;
    QString name;
    QStringList itemIds; ///< ordered audio item ids
};

struct JellyfinLibrarySnapshot {
    QList<JellyfinTrackInfo> tracks;
    QList<JellyfinPlaylistInfo> playlists;
};

struct JellyfinServerInfo {
    QString name;
    QString version;
    QString id;
};

struct JellyfinAuthResult {
    QString userId;
    QString userName;
    QString accessToken;
};

/// Minimal asynchronous client for the Jellyfin REST API (10.9+).
///
/// All requests use the modern
///   Authorization: MediaBrowser Client=..., Token=...
/// header; the legacy X-Emby-* headers and api_key query parameters are
/// disabled by default since Jellyfin 10.11.
///
/// Callbacks are invoked on the thread owning the client (the GUI thread)
/// and are never called after the client has been destroyed.
class JellyfinClient : public QObject {
    Q_OBJECT
  public:
    using ErrorCallback = std::function<void(const QString& message, bool unauthorized)>;

    explicit JellyfinClient(QObject* parent = nullptr);

    /// Adds a scheme if missing and strips trailing slashes.
    static QString normalizeServerUrl(const QString& url);

    /// Use server/token/user from the settings.
    void setCredentials(const JellyfinSettings& settings);
    void setCredentials(const QString& serverUrl,
            const QString& userId,
            const QString& accessToken,
            const QString& deviceId);

    QString serverUrl() const {
        return m_serverUrl;
    }

    /// GET /System/Info/Public (no auth needed)
    void fetchServerInfo(std::function<void(const JellyfinServerInfo&)> onSuccess,
            ErrorCallback onError);

    /// POST /Users/AuthenticateByName
    void authenticate(const QString& username,
            const QString& password,
            std::function<void(const JellyfinAuthResult&)> onSuccess,
            ErrorCallback onError);

    /// POST /Sessions/Logout — revokes the current token. Fire and forget.
    void logout();

    /// Fetch all audio items and all audio playlists of the user.
    void fetchLibrary(std::function<void(int done, int total)> onProgress,
            std::function<void(const JellyfinLibrarySnapshot&)> onSuccess,
            ErrorCallback onError);

    /// Abort a running fetchLibrary().
    void cancel();

    bool isBusy() const {
        return !m_pendingReply.isNull();
    }

    /// GET request for the original audio file of an item.
    QNetworkReply* startDownload(const QString& itemId);

  private:
    QUrl apiUrl(const QString& path, const QUrlQuery& query = QUrlQuery()) const;
    QNetworkRequest makeRequest(const QUrl& url) const;
    QByteArray authorizationHeader() const;

    /// GET a JSON document; callbacks are only called if not cancelled.
    void getJson(const QUrl& url,
            std::function<void(const QJsonDocument&)> onSuccess,
            ErrorCallback onError);
    void handleJsonReply(QNetworkReply* pReply,
            const std::function<void(const QJsonDocument&)>& onSuccess,
            const ErrorCallback& onError);

    // fetchLibrary() state machine
    struct FetchState;
    void fetchTracksPage(std::shared_ptr<FetchState> pState);
    void fetchPlaylists(std::shared_ptr<FetchState> pState);
    void fetchPlaylistItems(std::shared_ptr<FetchState> pState, int index);

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_pendingReply;
    quint64 m_generation = 0;

    QString m_serverUrl;
    QString m_userId;
    QString m_accessToken;
    QString m_deviceId;
};
