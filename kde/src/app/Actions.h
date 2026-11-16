// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>

namespace clauds {

class AccountStore;
class ChatWindows;
class LiveUsage;

/// The panel's buttons and menu items: asks the questions (same words as the macOS app), then
/// calls the store. Linux counterpart of the action methods of the macOS app.
class Actions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool startsAtLogin READ startsAtLogin NOTIFY startsAtLoginChanged)
public:
    Actions(AccountStore *store, LiveUsage *live, ChatWindows *chats, QObject *parent = nullptr);

    bool startsAtLogin() const;

    Q_INVOKABLE void adoptCurrent();
    Q_INVOKABLE void addAccount();
    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void signInForUsage(const QString &id, bool thenOpenClaude = false);
    Q_INVOKABLE void reloadUsage();
    Q_INVOKABLE void openChats(const QString &id);
    Q_INVOKABLE void newChat(const QString &id);
    Q_INVOKABLE void rename(const QString &id);
    Q_INVOKABLE void setEmail(const QString &id);
    Q_INVOKABLE void moveUp(const QString &id);
    Q_INVOKABLE void moveDown(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void setSideBySide(bool on);
    Q_INVOKABLE void setShareSessions(bool on);
    Q_INVOKABLE void setAutoSwitch(const QString &modeKey);
    Q_INVOKABLE void syncSessions();
    Q_INVOKABLE void setStartsAtLogin(bool on);
    Q_INVOKABLE void showSavedAccounts();
    Q_INVOKABLE void openClaude();
    Q_INVOKABLE void about();
    Q_INVOKABLE void quit();

Q_SIGNALS:
    /// A dialog is about to open: the panel hides so that it does not cover the dialog.
    void hidePanelRequested();
    void startsAtLoginChanged();

private:
    bool confirm(const QString &title, const QString &message, const QString &button, bool warning = false);
    QString askText(const QString &title, const QString &message, const QString &value, bool *ok);
    void showError(const QString &message);
    void notify(const QString &title, const QString &text);
    void switchTo(const QString &id);
    void show(const QString &id);
    QString nameOf(const QString &id) const;

    AccountStore *m_store;
    LiveUsage *m_live;
    ChatWindows *m_chats;
};

} // namespace clauds
