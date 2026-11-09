// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/usersettings.h"

class QLabel;
class QLineEdit;
class QPushButton;

/// Preferences → Jellyfin: account and audio cache settings.
class DlgPrefJellyfin : public DlgPreferencePage {
    Q_OBJECT
  public:
    DlgPrefJellyfin(QWidget* parent, UserSettingsPointer pConfig);

  public slots:
    void slotApply() override;
    void slotUpdate() override;
    void slotResetToDefaults() override;

  private slots:
    void slotLogin();
    void slotLogout();
    void slotBrowseCacheDir();
    void slotClearCache();

  private:
    void updateAccountState();
    void updateCacheUsage();

    UserSettingsPointer m_pConfig;

    QLabel* m_pAccountStatus;
    QPushButton* m_pLoginButton;
    QPushButton* m_pLogoutButton;

    QLineEdit* m_pCacheDir;
    QLabel* m_pCacheUsage;
};
