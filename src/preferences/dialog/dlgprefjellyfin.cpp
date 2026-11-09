// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "preferences/dialog/dlgprefjellyfin.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "library/jellyfin/dlgjellyfinlogin.h"
#include "library/jellyfin/jellyfincache.h"
#include "library/jellyfin/jellyfinclient.h"
#include "library/jellyfin/jellyfinsettings.h"
#include "moc_dlgprefjellyfin.cpp"

DlgPrefJellyfin::DlgPrefJellyfin(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(std::move(pConfig)),
          m_pAccountStatus(new QLabel(this)),
          m_pLoginButton(new QPushButton(this)),
          m_pLogoutButton(new QPushButton(tr("Log out"), this)),
          m_pCacheDir(new QLineEdit(this)),
          m_pCacheUsage(new QLabel(this)) {
    // Account
    auto* pAccountGroup = new QGroupBox(tr("Account"), this);
    m_pAccountStatus->setWordWrap(true);
    m_pAccountStatus->setTextFormat(Qt::PlainText);
    auto* pAccountButtons = new QHBoxLayout();
    pAccountButtons->addWidget(m_pLoginButton);
    pAccountButtons->addWidget(m_pLogoutButton);
    pAccountButtons->addStretch();
    auto* pAccountLayout = new QVBoxLayout(pAccountGroup);
    pAccountLayout->addWidget(m_pAccountStatus);
    pAccountLayout->addLayout(pAccountButtons);

    // Cache
    auto* pCacheGroup = new QGroupBox(tr("Audio Cache"), this);
    auto* pCacheInfo = new QLabel(
            tr("Mixxx can only play and analyze local files. Jellyfin tracks are "
               "downloaded into this folder when they are selected or loaded. "
               "Nothing is deleted automatically: use Clear Cache here or "
               "right-click an entry in the Jellyfin sidebar. Analysis, cues "
               "and loops are kept in the Mixxx library."),
            pCacheGroup);
    pCacheInfo->setWordWrap(true);

    auto* pBrowse = new QPushButton(tr("Browse…"), pCacheGroup);
    auto* pDirRow = new QHBoxLayout();
    pDirRow->addWidget(m_pCacheDir);
    pDirRow->addWidget(pBrowse);

    auto* pClear = new QPushButton(tr("Clear Cache"), pCacheGroup);
    auto* pUsageRow = new QHBoxLayout();
    pUsageRow->addWidget(m_pCacheUsage);
    pUsageRow->addStretch();
    pUsageRow->addWidget(pClear);

    auto* pCacheForm = new QFormLayout();
    pCacheForm->addRow(tr("Folder:"), pDirRow);
    pCacheForm->addRow(tr("In use:"), pUsageRow);

    auto* pCacheLayout = new QVBoxLayout(pCacheGroup);
    pCacheLayout->addWidget(pCacheInfo);
    pCacheLayout->addLayout(pCacheForm);

    auto* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(pAccountGroup);
    pLayout->addWidget(pCacheGroup);
    pLayout->addStretch();

    connect(m_pLoginButton, &QPushButton::clicked, this, &DlgPrefJellyfin::slotLogin);
    connect(m_pLogoutButton, &QPushButton::clicked, this, &DlgPrefJellyfin::slotLogout);
    connect(pBrowse, &QPushButton::clicked, this, &DlgPrefJellyfin::slotBrowseCacheDir);
    connect(pClear, &QPushButton::clicked, this, &DlgPrefJellyfin::slotClearCache);

    slotUpdate();
}

void DlgPrefJellyfin::updateAccountState() {
    const JellyfinSettings settings(m_pConfig);
    if (settings.isLoggedIn()) {
        const QString server = settings.serverName().isEmpty()
                ? settings.serverUrl()
                : QStringLiteral("%1 (%2)").arg(settings.serverName(), settings.serverUrl());
        m_pAccountStatus->setText(tr("Logged in as %1 on %2.").arg(settings.userName(), server));
        m_pLoginButton->setText(tr("Change Account…"));
        m_pLogoutButton->setVisible(true);
    } else {
        m_pAccountStatus->setText(
                tr("Not logged in. Log in to browse and play the music of "
                   "your Jellyfin server in the library sidebar."));
        m_pLoginButton->setText(tr("Log in…"));
        m_pLogoutButton->setVisible(false);
    }
}

void DlgPrefJellyfin::updateCacheUsage() {
    const qint64 bytes = JellyfinCache::diskUsage(JellyfinSettings(m_pConfig).cacheDir());
    m_pCacheUsage->setText(QLocale().formattedDataSize(bytes));
}

void DlgPrefJellyfin::slotUpdate() {
    const JellyfinSettings settings(m_pConfig);
    updateAccountState();
    m_pCacheDir->setText(settings.cacheDir());
    updateCacheUsage();
}

void DlgPrefJellyfin::slotApply() {
    JellyfinSettings settings(m_pConfig);
    const QString dir = m_pCacheDir->text().trimmed();
    settings.setCacheDir(dir.isEmpty() ? JellyfinSettings::defaultCacheDir() : dir);
}

void DlgPrefJellyfin::slotResetToDefaults() {
    m_pCacheDir->setText(JellyfinSettings::defaultCacheDir());
}

void DlgPrefJellyfin::slotLogin() {
    // The library feature notices the new account the next time it is
    // activated and syncs automatically.
    DlgJellyfinLogin::login(m_pConfig, this);
    updateAccountState();
}

void DlgPrefJellyfin::slotLogout() {
    JellyfinSettings settings(m_pConfig);
    auto* pClient = new JellyfinClient(this);
    pClient->setCredentials(settings);
    pClient->logout();
    settings.clearLogin();
    m_pConfig->save();
    updateAccountState();
}

void DlgPrefJellyfin::slotBrowseCacheDir() {
    const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose Jellyfin Cache Folder"), m_pCacheDir->text());
    if (!dir.isEmpty()) {
        m_pCacheDir->setText(dir);
    }
}

void DlgPrefJellyfin::slotClearCache() {
    const QString dir = JellyfinSettings(m_pConfig).cacheDir();
    const auto answer = QMessageBox::question(this,
            tr("Clear Jellyfin Cache"),
            tr("Delete all downloaded Jellyfin tracks from\n%1?\n\nTracks "
               "loaded in a deck are kept. Analysis data is not affected.")
                    .arg(dir));
    if (answer != QMessageBox::Yes) {
        return;
    }
    JellyfinCache::clear(dir);
    updateCacheUsage();
}
