// SPDX-License-Identifier: MIT
#include "Actions.h"
#include "AccountStore.h"
#include "ChatWindows.h"
#include "LiveUsage.h"
#include "LoginItem.h"
#include "Profiles.h"

#include <QApplication>
#include <QDesktopServices>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>

#ifdef CLAUDS_HAVE_KF6
#include <KNotification>
#endif

namespace clauds {

Actions::Actions(AccountStore *store, LiveUsage *live, ChatWindows *chats, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_live(live)
    , m_chats(chats)
{
    connect(m_store, &AccountStore::error, this, &Actions::showError);
    connect(m_store, &AccountStore::liveRefreshRequested, this, [this](const QStringList &ids) { m_live->refresh(m_store, ids); });
    connect(m_store, &AccountStore::autoSwitchProposed, this,
            [this](const QString &current, const QString &targetID, const QString &target, const QString &reason, int percent) {
                Q_EMIT hidePanelRequested();
                if (confirm(QStringLiteral("%1 is near a limit").arg(current),
                            QStringLiteral("%1 %2 has more room (%3% used).\n\nSwitch to %2? Claude quits and opens again "
                                           "with that account. Running Claude Code sessions in the app stop.")
                                .arg(reason, target)
                                .arg(percent),
                            QStringLiteral("Switch")))
                    m_store->switchTo(targetID);
            });
    connect(m_store, &AccountStore::autoSwitched, this, [this](const QString &note) { notify(QStringLiteral("Clauds"), note); });
}

QString Actions::nameOf(const QString &id) const
{
    const auto a = m_store->account(id);
    return a ? a->name : QString();
}

bool Actions::confirm(const QString &title, const QString &message, const QString &button, bool warning)
{
    Q_EMIT hidePanelRequested();
    QMessageBox box(warning ? QMessageBox::Warning : QMessageBox::Question, title, message);
    auto *yes = box.addButton(button, QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(warning ? qobject_cast<QPushButton *>(box.button(QMessageBox::Cancel)) : yes);
    box.exec();
    return box.clickedButton() == yes;
}

QString Actions::askText(const QString &title, const QString &message, const QString &value, bool *ok)
{
    Q_EMIT hidePanelRequested();
    const QString text = QInputDialog::getText(nullptr, title, message, QLineEdit::Normal, value, ok).trimmed();
    if (*ok && text.isEmpty())
        *ok = false;
    return text;
}

void Actions::showError(const QString &message)
{
    Q_EMIT hidePanelRequested();
    QMessageBox::warning(nullptr, QStringLiteral("Clauds"), message);
}

void Actions::notify(const QString &title, const QString &text)
{
#ifdef CLAUDS_HAVE_KF6
    KNotification::event(QStringLiteral("autoSwitched"), title, text, QStringLiteral("clauds"));
#else
    Q_UNUSED(title)
    Q_UNUSED(text)
#endif
}

bool Actions::startsAtLogin() const { return LoginItem::isOn(); }

void Actions::adoptCurrent()
{
    bool ok = false;
    const QString name = askText(QStringLiteral("Name this account"),
                                 QStringLiteral("Clauds saves the login that the Claude app has now. Use a name you know, "
                                                "for example your email."),
                                 {}, &ok);
    if (ok)
        m_store->adoptCurrentLogin(name);
}

void Actions::addAccount()
{
    bool ok = false;
    const QString name = askText(QStringLiteral("Add account"),
                                 QStringLiteral("A sign-in window opens next. Sign in to this account there. The window "
                                                "closes by itself when the login is good. Then Claude opens for this "
                                                "account: sign in there one time too."),
                                 {}, &ok);
    if (!ok)
        return;
    const QString id = m_store->addAccount(name);
    // In Side by Side, the account's own Claude copy opens after the usage sign-in.
    signInForUsage(id, m_store->sideBySide());
}

void Actions::signInForUsage(const QString &id, bool thenOpenClaude)
{
    Q_EMIT hidePanelRequested();
    m_chats->openForSignIn(id, nameOf(id), [this, id, thenOpenClaude] {
        m_live->refresh(m_store, {id});
        if (thenOpenClaude)
            show(id);
    });
}

void Actions::reloadUsage()
{
    QStringList ids;
    for (const auto &a : m_store->accounts())
        ids << a.id;
    m_live->refresh(m_store, ids);
}

void Actions::openChats(const QString &id)
{
    Q_EMIT hidePanelRequested();
    m_chats->open(id, nameOf(id));
}

void Actions::newChat(const QString &id)
{
    Q_EMIT hidePanelRequested();
    m_chats->open(id, nameOf(id), QStringLiteral("/new"));
}

void Actions::rename(const QString &id)
{
    bool ok = false;
    const QString name = askText(QStringLiteral("Rename account"), {}, nameOf(id), &ok);
    if (ok)
        m_store->rename(id, name);
}

void Actions::setEmail(const QString &id)
{
    const auto a = m_store->account(id);
    if (!a)
        return;
    Q_EMIT hidePanelRequested();
    bool ok = false;
    const QString email = QInputDialog::getText(nullptr, QStringLiteral("Email for %1").arg(a->name),
                                                QStringLiteral("Only shown in the panel, under the account name."),
                                                QLineEdit::Normal, a->email, &ok);
    if (ok)
        m_store->setEmail(id, email.trimmed());
}

void Actions::moveUp(const QString &id) { m_store->move(id, -1); }
void Actions::moveDown(const QString &id) { m_store->move(id, 1); }

void Actions::remove(const QString &id)
{
    if (!confirm(QStringLiteral("Remove %1?").arg(nameOf(id)),
                 QStringLiteral("Its saved Claude app data goes to the Trash. Its chat window login is deleted. You can "
                                "sign in again later."),
                 QStringLiteral("Remove")))
        return;
    m_chats->close(id);
    if (m_store->remove(id)) {
        m_live->forget(id);
        Profiles::forget(id);
    }
}

void Actions::select(const QString &id)
{
    if (m_store->sideBySide())
        show(id);
    else
        switchTo(id);
}

void Actions::show(const QString &id)
{
    Q_EMIT hidePanelRequested();
    m_store->showSideBySide(id);
}

void Actions::switchTo(const QString &id)
{
    if (id == m_store->activeID()) {
        Q_EMIT hidePanelRequested();
        m_store->openClaude();
        return;
    }
    if (m_store->isClaudeRunning()
        && !confirm(QStringLiteral("Switch to %1?").arg(nameOf(id)),
                    QStringLiteral("Claude quits and opens again with this account. Running Claude Code sessions in the "
                                   "app stop."),
                    QStringLiteral("Switch")))
        return;
    m_store->switchTo(id);
}

void Actions::setSideBySide(bool on)
{
    if (m_store->isClaudeRunning()) {
        const QString message = on
            ? (m_store->shareInSideBySide()
                   ? QStringLiteral("All Claude copies quit, and the main one opens again. Running Code sessions stop. "
                                    "Code sessions stay shared, because you turned on \"Share Code Sessions\" in Side by "
                                    "Side before. That is risky: turn it off if you do not want it.")
                   : QStringLiteral("All Claude copies quit, and the main one opens again. Running Code sessions stop. "
                                    "For safety, each account then gets its own separate copy of the Code sessions."))
            : QStringLiteral("All Claude copies quit, and the main one opens again. Running Code sessions stop. Code "
                             "session sharing turns on again. For each session, the newer version is kept.");
        if (!confirm(on ? QStringLiteral("Turn on Side by Side?") : QStringLiteral("Turn off Side by Side?"), message,
                     QStringLiteral("Restart Claude")))
            return;
    }
    m_store->setSideBySide(on);
}

void Actions::setShareSessions(bool on)
{
    if (m_store->sideBySide() && on) {
        // Always shown, also when Claude is closed: this setting can damage sessions.
        if (!confirm(QStringLiteral("Share Code sessions in Side by Side?"),
                     QStringLiteral("This is not safe. With this on, all open Claude copies write the same session "
                                    "files.\n\nWhat can go wrong:\n"
                                    "• If the same session is open in two accounts, it can lose messages, split in two, "
                                    "or go back to an old state.\n"
                                    "• A session that you archive or rename in one copy can come back or change in a "
                                    "different copy.\n"
                                    "• A new session can stay hidden in the other copies until they restart.\n"
                                    "• The app cannot know which account made a new session. It gives new sessions to "
                                    "the main account.\n\n"
                                    "Rule to stay safe: work on one session in one account at a time.\n\n"
                                    "All Claude copies quit now, and the main one opens again. Running Code sessions "
                                    "stop."),
                     QStringLiteral("Share Anyway"), true))
            return;
        m_store->setShareSessions(true);
        return;
    }
    if (m_store->isClaudeRunning()
        && !confirm(on ? QStringLiteral("Share Code sessions?") : QStringLiteral("Stop sharing Code sessions?"),
                    on ? QStringLiteral("Claude quits and opens again. Then every account uses the same Code sessions. "
                                        "For each session, the newer version is kept.")
                       : QStringLiteral("Claude quits and opens again. Then each account has its own separate copy of "
                                        "the Code sessions."),
                    QStringLiteral("Restart Claude")))
        return;
    m_store->setShareSessions(on);
}

void Actions::setAutoSwitch(const QString &modeKey)
{
    if (modeKey == QLatin1String("auto")
        && !confirm(QStringLiteral("Switch automatically?"),
                    QStringLiteral("When the current account reaches %1% of a limit, Clauds goes to the account with the "
                                   "most room. It does not ask. Claude quits and opens again, and running Claude Code "
                                   "sessions in the app stop.\n\nUse \"Ask Me First\" if you want to decide each time.")
                        .arg(AutoSwitch::threshold),
                    QStringLiteral("Switch Automatically")))
        return;
    m_store->setAutoSwitch(modeKey);
}

void Actions::syncSessions()
{
    if (m_store->isClaudeRunning()
        && !confirm(QStringLiteral("Share Code sessions now?"),
                    QStringLiteral("All Claude copies quit, and the main one opens again. Then every account uses the "
                                   "same Code sessions. Running Code sessions stop."),
                    QStringLiteral("Restart Claude")))
        return;
    m_store->syncSessionsNow();
}

void Actions::setStartsAtLogin(bool on)
{
    QString error;
    if (!LoginItem::set(on, &error))
        showError(error);
    Q_EMIT startsAtLoginChanged();
}

void Actions::showSavedAccounts()
{
    Q_EMIT hidePanelRequested();
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->profilesDir()));
}

void Actions::openClaude()
{
    Q_EMIT hidePanelRequested();
    m_store->openClaude();
}

void Actions::about()
{
    Q_EMIT hidePanelRequested();
    QMessageBox::about(nullptr, QStringLiteral("About Clauds"),
                       QStringLiteral("<h3>Clauds for KDE Plasma %1</h3>"
                                      "<p>Switches the Claude desktop app between accounts. Every account stays signed in.</p>"
                                      "<p>KDE port of <a href=\"https://github.com/codeFi/clauds\">Clauds</a> by Razvan Tanase. "
                                      "MIT License.</p>"
                                      "<p>Clauds is an independent project. It is not made, endorsed, or supported by "
                                      "Anthropic. \"Claude\" is a trademark of Anthropic, PBC.</p>")
                           .arg(QStringLiteral(CLAUDS_VERSION)));
}

void Actions::quit() { QApplication::quit(); }

} // namespace clauds
