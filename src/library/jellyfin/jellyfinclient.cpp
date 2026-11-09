// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/jellyfinclient.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include <QtDebug>

#include "library/jellyfin/jellyfinsettings.h"
#include "moc_jellyfinclient.cpp"
#include "util/versionstore.h"

namespace {

constexpr int kPageSize = 1000;
constexpr int kApiTimeoutMs = 30 * 1000;
constexpr int kDownloadInactivityTimeoutMs = 60 * 1000;

QString joinStrings(const QJsonValue& value) {
    QStringList parts;
    const QJsonArray array = value.toArray();
    for (const auto& item : array) {
        const QString s = item.toString().trimmed();
        if (!s.isEmpty()) {
            parts.append(s);
        }
    }
    return parts.join(QStringLiteral(", "));
}

QString extensionFor(const QJsonObject& item) {
    QString ext = QFileInfo(item.value(QStringLiteral("Path")).toString()).suffix().toLower();
    if (!ext.isEmpty()) {
        return ext;
    }
    // Container may be a comma separated list like "mov,mp4,m4a,3gp,3g2,mj2"
    const QStringList containers =
            item.value(QStringLiteral("Container")).toString().toLower().split(QChar(','));
    if (containers.contains(QStringLiteral("m4a"))) {
        return QStringLiteral("m4a");
    }
    if (!containers.isEmpty() && !containers.first().isEmpty()) {
        return containers.first();
    }
    return QStringLiteral("mp3");
}

JellyfinTrackInfo parseTrack(const QJsonObject& item) {
    JellyfinTrackInfo track;
    track.id = item.value(QStringLiteral("Id")).toString();
    track.title = item.value(QStringLiteral("Name")).toString();
    track.album = item.value(QStringLiteral("Album")).toString();
    track.albumArtist = item.value(QStringLiteral("AlbumArtist")).toString();
    track.artist = joinStrings(item.value(QStringLiteral("Artists")));
    if (track.artist.isEmpty()) {
        track.artist = track.albumArtist;
    }
    track.genre = joinStrings(item.value(QStringLiteral("Genres")));
    track.year = item.value(QStringLiteral("ProductionYear")).toInt();
    track.trackNumber = item.value(QStringLiteral("IndexNumber")).toInt();
    track.discNumber = item.value(QStringLiteral("ParentIndexNumber")).toInt();
    track.durationSecs = item.value(QStringLiteral("RunTimeTicks")).toDouble() / 1e7;
    track.dateAdded = item.value(QStringLiteral("DateCreated")).toString();
    track.extension = extensionFor(item);
    return track;
}

} // namespace

struct JellyfinClient::FetchState {
    quint64 generation = 0;
    int total = 0;
    JellyfinLibrarySnapshot snapshot;
    std::function<void(int, int)> onProgress;
    std::function<void(const JellyfinLibrarySnapshot&)> onSuccess;
    ErrorCallback onError;
};

JellyfinClient::JellyfinClient(QObject* parent)
        : QObject(parent) {
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

// static
QString JellyfinClient::normalizeServerUrl(const QString& url) {
    QString result = url.trimmed();
    if (result.isEmpty()) {
        return result;
    }
    // The address may be entered with or without "http://" / "https://".
    if (!result.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
            !result.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        result.prepend(QStringLiteral("http://"));
    }
    while (result.endsWith(QChar('/'))) {
        result.chop(1);
    }
    return result;
}

void JellyfinClient::setCredentials(const JellyfinSettings& settings) {
    setCredentials(settings.serverUrl(),
            settings.userId(),
            settings.accessToken(),
            settings.deviceId());
}

void JellyfinClient::setCredentials(const QString& serverUrl,
        const QString& userId,
        const QString& accessToken,
        const QString& deviceId) {
    m_serverUrl = normalizeServerUrl(serverUrl);
    m_userId = userId;
    m_accessToken = accessToken;
    m_deviceId = deviceId;
}

QUrl JellyfinClient::apiUrl(const QString& path, const QUrlQuery& query) const {
    QUrl url(m_serverUrl + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    return url;
}

QByteArray JellyfinClient::authorizationHeader() const {
    auto quoted = [](const QString& s) -> QString {
        QString escaped = s;
        escaped.remove(QChar('"'));
        return QStringLiteral("\"") + escaped + QStringLiteral("\"");
    };
    QString header = QStringLiteral("MediaBrowser Client=") + quoted(QStringLiteral("Mixxx")) +
            QStringLiteral(", Device=") + quoted(QSysInfo::machineHostName()) +
            QStringLiteral(", DeviceId=") + quoted(m_deviceId) +
            QStringLiteral(", Version=") + quoted(VersionStore::version());
    if (!m_accessToken.isEmpty()) {
        header += QStringLiteral(", Token=") + quoted(m_accessToken);
    }
    return header.toUtf8();
}

QNetworkRequest JellyfinClient::makeRequest(const QUrl& url) const {
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authorizationHeader());
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(kApiTimeoutMs);
    return request;
}

void JellyfinClient::handleJsonReply(QNetworkReply* pReply,
        const std::function<void(const QJsonDocument&)>& onSuccess,
        const ErrorCallback& onError) {
    pReply->deleteLater();
    const int status = pReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        onError(tr("The server rejected the credentials (HTTP %1).").arg(status), true);
        return;
    }
    if (pReply->error() != QNetworkReply::NoError) {
        if (pReply->error() == QNetworkReply::OperationCanceledError) {
            onError(tr("Request timed out or was cancelled."), false);
        } else {
            onError(pReply->errorString(), false);
        }
        return;
    }
    const QByteArray body = pReply->readAll();
    if (body.isEmpty()) {
        onSuccess(QJsonDocument());
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        onError(tr("Unexpected response from server: %1").arg(parseError.errorString()), false);
        return;
    }
    onSuccess(doc);
}

void JellyfinClient::getJson(const QUrl& url,
        std::function<void(const QJsonDocument&)> onSuccess,
        ErrorCallback onError) {
    QNetworkReply* pReply = m_nam.get(makeRequest(url));
    m_pendingReply = pReply;
    const quint64 generation = m_generation;
    connect(pReply,
            &QNetworkReply::finished,
            this,
            [this, pReply, generation, onSuccess, onError]() {
                if (generation != m_generation) {
                    pReply->deleteLater();
                    return;
                }
                handleJsonReply(pReply, onSuccess, onError);
            });
}

// ---------------------------------------------------------------------------

void JellyfinClient::fetchServerInfo(
        std::function<void(const JellyfinServerInfo&)> onSuccess,
        ErrorCallback onError) {
    const QUrl url = apiUrl(QStringLiteral("/System/Info/Public"));
    if (!url.isValid() || url.host().isEmpty()) {
        onError(tr("Invalid server address."), false);
        return;
    }
    getJson(
            url,
            [onSuccess, onError](const QJsonDocument& doc) {
                const QJsonObject obj = doc.object();
                JellyfinServerInfo info;
                info.name = obj.value(QStringLiteral("ServerName")).toString();
                info.version = obj.value(QStringLiteral("Version")).toString();
                info.id = obj.value(QStringLiteral("Id")).toString();
                if (info.id.isEmpty()) {
                    onError(tr("This does not look like a Jellyfin server."), false);
                    return;
                }
                onSuccess(info);
            },
            onError);
}

void JellyfinClient::authenticate(const QString& username,
        const QString& password,
        std::function<void(const JellyfinAuthResult&)> onSuccess,
        ErrorCallback onError) {
    const QUrl url = apiUrl(QStringLiteral("/Users/AuthenticateByName"));
    if (!url.isValid() || url.host().isEmpty()) {
        onError(tr("Invalid server address."), false);
        return;
    }
    QNetworkRequest request = makeRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body.insert(QStringLiteral("Username"), username);
    body.insert(QStringLiteral("Pw"), password);

    QNetworkReply* pReply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_pendingReply = pReply;
    const quint64 generation = m_generation;
    connect(pReply,
            &QNetworkReply::finished,
            this,
            [this, pReply, generation, onSuccess, onError]() {
                if (generation != m_generation) {
                    pReply->deleteLater();
                    return;
                }
                handleJsonReply(
                        pReply,
                        [onSuccess, onError](const QJsonDocument& doc) {
                            const QJsonObject obj = doc.object();
                            const QJsonObject user = obj.value(QStringLiteral("User")).toObject();
                            JellyfinAuthResult result;
                            result.accessToken =
                                    obj.value(QStringLiteral("AccessToken"))
                                            .toString();
                            result.userId = user.value(QStringLiteral("Id")).toString();
                            result.userName = user.value(QStringLiteral("Name")).toString();
                            if (result.accessToken.isEmpty() || result.userId.isEmpty()) {
                                onError(tr("Server response is missing the access token."),
                                        false);
                                return;
                            }
                            onSuccess(result);
                        },
                        [onError](const QString& message, bool unauthorized) {
                            if (unauthorized) {
                                onError(tr("Wrong user name or password."), true);
                            } else {
                                onError(message, false);
                            }
                        });
            });
}

void JellyfinClient::logout() {
    if (m_serverUrl.isEmpty() || m_accessToken.isEmpty()) {
        return;
    }
    QNetworkReply* pReply = m_nam.post(
            makeRequest(apiUrl(QStringLiteral("/Sessions/Logout"))), QByteArray());
    connect(pReply, &QNetworkReply::finished, pReply, &QObject::deleteLater);
}

void JellyfinClient::cancel() {
    ++m_generation;
    if (m_pendingReply) {
        m_pendingReply->abort();
        m_pendingReply = nullptr;
    }
}

QNetworkReply* JellyfinClient::startDownload(const QString& itemId) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("static"), QStringLiteral("true"));
    QNetworkRequest request = makeRequest(
            apiUrl(QStringLiteral("/Audio/%1/stream").arg(itemId), query));
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(kDownloadInactivityTimeoutMs);
    return m_nam.get(request);
}

// ---------------------------------------------------------------------------
// fetchLibrary()

void JellyfinClient::fetchLibrary(std::function<void(int, int)> onProgress,
        std::function<void(const JellyfinLibrarySnapshot&)> onSuccess,
        ErrorCallback onError) {
    cancel();
    auto pState = std::make_shared<FetchState>();
    pState->generation = m_generation;
    pState->onProgress = std::move(onProgress);
    pState->onSuccess = std::move(onSuccess);
    pState->onError = std::move(onError);
    fetchTracksPage(pState);
}

void JellyfinClient::fetchTracksPage(std::shared_ptr<FetchState> pState) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_userId);
    query.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("Audio"));
    query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Path,Genres,DateCreated"));
    query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    query.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("EnableTotalRecordCount"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("StartIndex"),
            QString::number(pState->snapshot.tracks.size()));
    query.addQueryItem(QStringLiteral("Limit"), QString::number(kPageSize));

    getJson(
            apiUrl(QStringLiteral("/Items"), query),
            [this, pState](const QJsonDocument& doc) {
                const QJsonObject obj = doc.object();
                const QJsonArray items = obj.value(QStringLiteral("Items")).toArray();
                pState->total = obj.value(QStringLiteral("TotalRecordCount")).toInt();
                for (const auto& item : items) {
                    pState->snapshot.tracks.append(parseTrack(item.toObject()));
                }
                if (pState->onProgress) {
                    pState->onProgress(static_cast<int>(pState->snapshot.tracks.size()),
                            pState->total);
                }
                if (items.isEmpty() || pState->snapshot.tracks.size() >= pState->total) {
                    fetchPlaylists(pState);
                } else {
                    fetchTracksPage(pState);
                }
            },
            pState->onError);
}

void JellyfinClient::fetchPlaylists(std::shared_ptr<FetchState> pState) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_userId);
    query.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("Playlist"));
    query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    query.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));

    getJson(
            apiUrl(QStringLiteral("/Items"), query),
            [this, pState](const QJsonDocument& doc) {
                const QJsonArray items = doc.object().value(QStringLiteral("Items")).toArray();
                for (const auto& value : items) {
                    const QJsonObject item = value.toObject();
                    const QString mediaType = item.value(QStringLiteral("MediaType")).toString();
                    if (mediaType == QStringLiteral("Video")) {
                        continue;
                    }
                    JellyfinPlaylistInfo playlist;
                    playlist.id = item.value(QStringLiteral("Id")).toString();
                    playlist.name = item.value(QStringLiteral("Name")).toString();
                    pState->snapshot.playlists.append(playlist);
                }
                fetchPlaylistItems(pState, 0);
            },
            pState->onError);
}

void JellyfinClient::fetchPlaylistItems(std::shared_ptr<FetchState> pState, int index) {
    if (index >= pState->snapshot.playlists.size()) {
        m_pendingReply = nullptr;
        pState->onSuccess(pState->snapshot);
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_userId);
    query.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));

    const QString playlistId = pState->snapshot.playlists.at(index).id;
    getJson(
            apiUrl(QStringLiteral("/Playlists/%1/Items").arg(playlistId), query),
            [this, pState, index](const QJsonDocument& doc) {
                const QJsonArray items = doc.object().value(QStringLiteral("Items")).toArray();
                QStringList& ids = pState->snapshot.playlists[index].itemIds;
                for (const auto& value : items) {
                    const QJsonObject item = value.toObject();
                    if (item.value(QStringLiteral("Type")).toString() == QStringLiteral("Audio")) {
                        ids.append(item.value(QStringLiteral("Id")).toString());
                    }
                }
                fetchPlaylistItems(pState, index + 1);
            },
            [this, pState, index](const QString& message, bool unauthorized) {
                if (unauthorized) {
                    pState->onError(message, true);
                    return;
                }
                // A single broken playlist should not fail the whole sync.
                qWarning() << "Jellyfin: failed to fetch playlist"
                           << pState->snapshot.playlists.at(index).name << message;
                fetchPlaylistItems(pState, index + 1);
            });
}
