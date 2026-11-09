// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#pragma once

#include <QDialog>

#include "preferences/usersettings.h"

class JellyfinClient;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

/// Asks for server address, user name and password and logs in.
/// On success the server URL, user and access token are stored in the
/// [Jellyfin] config group and the dialog is accepted. The password is not
/// stored.
class DlgJellyfinLogin : public QDialog {
    Q_OBJECT
  public:
    DlgJellyfinLogin(UserSettingsPointer pConfig, QWidget* parent = nullptr);

    /// Convenience: runs the dialog modally, returns true on successful login.
    static bool login(UserSettingsPointer pConfig, QWidget* parent);

  private slots:
    void slotLogin();
    void slotInputChanged();

  private:
    void setBusy(bool busy);
    void showError(const QString& message);

    UserSettingsPointer m_pConfig;
    JellyfinClient* m_pClient;
    QLineEdit* m_pServerUrl;
    QLineEdit* m_pUserName;
    QLineEdit* m_pPassword;
    QLabel* m_pStatus;
    QDialogButtonBox* m_pButtons;
};
