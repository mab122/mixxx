// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/jellyfinsettings.h"

#include <QDir>
#include <QStandardPaths>
#include <QUuid>

namespace {

const QString kGroup = QStringLiteral("[Jellyfin]");

ConfigKey key(const char* name) {
    return ConfigKey(kGroup, QString::fromLatin1(name));
}

} // namespace

JellyfinSettings::JellyfinSettings(UserSettingsPointer pConfig)
        : m_pConfig(std::move(pConfig)) {
    // An earlier prototype stored the plain text password; never keep it.
    if (m_pConfig->exists(key("Password"))) {
        m_pConfig->remove(key("Password"));
        m_pConfig->save();
    }
}

QString JellyfinSettings::serverUrl() const {
    return m_pConfig->getValueString(key("ServerUrl"));
}

void JellyfinSettings::setServerUrl(const QString& url) {
    m_pConfig->setValue(key("ServerUrl"), url);
}

QString JellyfinSettings::serverName() const {
    return m_pConfig->getValueString(key("ServerName"));
}

void JellyfinSettings::setServerName(const QString& name) {
    m_pConfig->setValue(key("ServerName"), name);
}

QString JellyfinSettings::userId() const {
    return m_pConfig->getValueString(key("UserId"));
}

void JellyfinSettings::setUserId(const QString& id) {
    m_pConfig->setValue(key("UserId"), id);
}

QString JellyfinSettings::userName() const {
    return m_pConfig->getValueString(key("UserName"));
}

void JellyfinSettings::setUserName(const QString& name) {
    m_pConfig->setValue(key("UserName"), name);
}

QString JellyfinSettings::accessToken() const {
    return m_pConfig->getValueString(key("AccessToken"));
}

void JellyfinSettings::setAccessToken(const QString& token) {
    m_pConfig->setValue(key("AccessToken"), token);
}

QString JellyfinSettings::deviceId() const {
    QString id = m_pConfig->getValueString(key("DeviceId"));
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_pConfig->setValue(key("DeviceId"), id);
    }
    return id;
}

QString JellyfinSettings::defaultCacheDir() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath(QStringLiteral("jellyfin"));
}

QString JellyfinSettings::cacheDir() const {
    const QString dir = m_pConfig->getValueString(key("CacheDir"));
    return dir.isEmpty() ? defaultCacheDir() : dir;
}

void JellyfinSettings::setCacheDir(const QString& dir) {
    m_pConfig->setValue(key("CacheDir"), dir == defaultCacheDir() ? QString() : dir);
}

bool JellyfinSettings::isLoggedIn() const {
    return !serverUrl().isEmpty() && !accessToken().isEmpty() && !userId().isEmpty();
}

void JellyfinSettings::clearLogin() {
    setAccessToken(QString());
    setUserId(QString());
    setUserName(QString());
}
