// AI-generated ("vibecoded") code: written by an AI agent (Claude, Anthropic)
// at the request of mab122 for an unofficial Jellyfin fork of Mixxx. It has
// not been reviewed or endorsed by the Mixxx project.

#include "library/jellyfin/dlgjellyfinlogin.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "library/jellyfin/jellyfinclient.h"
#include "library/jellyfin/jellyfinsettings.h"
#include "moc_dlgjellyfinlogin.cpp"

DlgJellyfinLogin::DlgJellyfinLogin(UserSettingsPointer pConfig, QWidget* parent)
        : QDialog(parent),
          m_pConfig(std::move(pConfig)),
          m_pClient(new JellyfinClient(this)),
          m_pServerUrl(new QLineEdit(this)),
          m_pUserName(new QLineEdit(this)),
          m_pPassword(new QLineEdit(this)),
          m_pStatus(new QLabel(this)),
          m_pButtons(new QDialogButtonBox(
                  QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)) {
    setWindowTitle(tr("Log in to Jellyfin"));
    setMinimumWidth(420);

    const JellyfinSettings settings(m_pConfig);
    m_pServerUrl->setPlaceholderText(QStringLiteral("https://jellyfin.example.com"));
    m_pServerUrl->setText(settings.serverUrl());
    m_pUserName->setText(settings.userName());
    m_pPassword->setEchoMode(QLineEdit::Password);

    m_pStatus->setWordWrap(true);
    m_pStatus->setTextFormat(Qt::PlainText);

    auto* pIntro = new QLabel(
            tr("Enter the address of your Jellyfin server, e.g. "
               "http://192.168.1.10:8096 or https://jellyfin.example.com."),
            this);
    pIntro->setWordWrap(true);

    auto* pForm = new QFormLayout();
    pForm->addRow(tr("Server address:"), m_pServerUrl);
    pForm->addRow(tr("User name:"), m_pUserName);
    pForm->addRow(tr("Password:"), m_pPassword);

    m_pButtons->button(QDialogButtonBox::Ok)->setText(tr("Log in"));

    auto* pLayout = new QVBoxLayout(this);
    pLayout->addWidget(pIntro);
    pLayout->addLayout(pForm);
    pLayout->addWidget(m_pStatus);
    pLayout->addWidget(m_pButtons);

    connect(m_pButtons, &QDialogButtonBox::accepted, this, &DlgJellyfinLogin::slotLogin);
    connect(m_pButtons, &QDialogButtonBox::rejected, this, [this]() {
        m_pClient->cancel();
        reject();
    });
    connect(m_pServerUrl, &QLineEdit::textChanged, this, &DlgJellyfinLogin::slotInputChanged);
    connect(m_pUserName, &QLineEdit::textChanged, this, &DlgJellyfinLogin::slotInputChanged);
    slotInputChanged();

    if (m_pServerUrl->text().isEmpty()) {
        m_pServerUrl->setFocus();
    } else if (m_pUserName->text().isEmpty()) {
        m_pUserName->setFocus();
    } else {
        m_pPassword->setFocus();
    }
}

// static
bool DlgJellyfinLogin::login(UserSettingsPointer pConfig, QWidget* parent) {
    DlgJellyfinLogin dialog(std::move(pConfig), parent);
    return dialog.exec() == QDialog::Accepted;
}

void DlgJellyfinLogin::slotInputChanged() {
    m_pButtons->button(QDialogButtonBox::Ok)
            ->setEnabled(!m_pServerUrl->text().trimmed().isEmpty() &&
                    !m_pUserName->text().trimmed().isEmpty());
}

void DlgJellyfinLogin::setBusy(bool busy) {
    m_pServerUrl->setEnabled(!busy);
    m_pUserName->setEnabled(!busy);
    m_pPassword->setEnabled(!busy);
    m_pButtons->button(QDialogButtonBox::Ok)->setEnabled(!busy);
}

void DlgJellyfinLogin::showError(const QString& message) {
    setBusy(false);
    m_pStatus->setText(tr("Login failed: %1").arg(message));
    slotInputChanged();
}

void DlgJellyfinLogin::slotLogin() {
    const QString serverUrl = JellyfinClient::normalizeServerUrl(m_pServerUrl->text());
    const QString userName = m_pUserName->text().trimmed();
    const QString password = m_pPassword->text();
    const QString deviceId = JellyfinSettings(m_pConfig).deviceId();

    m_pServerUrl->setText(serverUrl); // show what is actually used
    setBusy(true);
    m_pStatus->setText(tr("Connecting to %1…").arg(serverUrl));
    m_pClient->setCredentials(serverUrl, QString(), QString(), deviceId);

    m_pClient->fetchServerInfo(
            [this, serverUrl, userName, password](const JellyfinServerInfo& info) {
                m_pStatus->setText(tr("Connected to %1 (Jellyfin %2). Logging in…")
                                .arg(info.name, info.version));
                m_pClient->authenticate(
                        userName,
                        password,
                        [this, serverUrl, info](const JellyfinAuthResult& auth) {
                            JellyfinSettings settings(m_pConfig);
                            settings.setServerUrl(serverUrl);
                            settings.setServerName(info.name);
                            settings.setUserId(auth.userId);
                            settings.setUserName(auth.userName);
                            settings.setAccessToken(auth.accessToken);
                            m_pConfig->save();
                            accept();
                        },
                        [this](const QString& message, bool) {
                            showError(message);
                        });
            },
            [this](const QString& message, bool) {
                showError(message);
            });
}
