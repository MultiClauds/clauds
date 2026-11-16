// SPDX-License-Identifier: MIT
#pragma once

#include "Account.h"
#include "AutoSwitch.h"
#include "ClaudeProcess.h"
#include "Usage.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <optional>

namespace clauds {

/// Keeps one Claude desktop data folder per account (Linux port of AccountStore.swift).
///
/// The active account's data lives where Claude expects it:
///   ~/.config/Claude
/// Every other account's data waits in:
///   ~/.local/share/clauds/Profiles/<account id>
///
/// A switch quits Claude, renames the folders (instant, same disk), and opens Claude again.
class AccountStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accountsModel NOTIFY changed)
    Q_PROPERTY(QString activeID READ activeID NOTIFY changed)
    Q_PROPERTY(bool isSwitching READ isSwitching NOTIFY changed)
    Q_PROPERTY(bool sideBySide READ sideBySide NOTIFY changed)
    Q_PROPERTY(bool shareSessions READ shareSessions NOTIFY changed)
    Q_PROPERTY(bool shareInSideBySide READ shareInSideBySide NOTIFY changed)
    Q_PROPERTY(bool sessionsAreShared READ sessionsAreShared NOTIFY changed)
    Q_PROPERTY(QString autoSwitch READ autoSwitchKey NOTIFY changed)
    Q_PROPERTY(bool claudeInstalled READ claudeInstalled CONSTANT)
    Q_PROPERTY(bool claudeRunning READ isClaudeRunning NOTIFY changed)
    Q_PROPERTY(QString autoSwitchNote READ autoSwitchNote NOTIFY changed)

public:
    enum class LiveState { Unknown, Ok, NeedsSignIn, Failed };
    Q_ENUM(LiveState)

    struct Paths {
        QString claudeDir;    ///< Claude's own data folder (~/.config/Claude)
        QString supportDir;   ///< this app's folder (~/.local/share/clauds)
        static Paths standard();
    };

    explicit AccountStore(const Paths &paths, ClaudeProcess claude, QObject *parent = nullptr);
    ~AccountStore() override;

    const QList<Account> &accounts() const { return m_accounts; }
    QString activeID() const { return m_activeID; }
    std::optional<Account> account(const QString &id) const;
    bool isSwitching() const { return m_switching; }
    bool sideBySide() const { return m_sideBySide; }
    bool shareSessions() const { return m_shareSessions; }
    bool shareInSideBySide() const { return m_shareInSideBySide; }
    bool sessionsAreShared() const { return m_shareSessions && (!m_sideBySide || m_shareInSideBySide); }
    AutoSwitch::Mode autoSwitch() const { return m_autoSwitch; }
    QString autoSwitchKey() const { return AutoSwitch::modeKey(m_autoSwitch); }
    bool claudeInstalled() const { return m_claude.isInstalled(); }
    bool isClaudeRunning() const { return m_claude.anyRunning(); }
    QString autoSwitchNote() const { return m_autoSwitchNote; }
    const QHash<QString, Usage> &usage() const { return m_usage; }
    QSet<QString> runningIDs() const { return m_running; }

    QString claudeDir() const { return m_paths.claudeDir; }
    QString supportDir() const { return m_paths.supportDir; }
    QString profilesDir() const { return m_paths.supportDir + QStringLiteral("/Profiles"); }
    QString sharedSessionsDir() const { return m_paths.supportDir + QStringLiteral("/SharedSessions"); }
    QString profileDir(const QString &id) const { return profilesDir() + QLatin1Char('/') + id; }
    /// The folder that holds this account's data now.
    QString dataDir(const QString &id) const { return id == m_activeID ? claudeDir() : profileDir(id); }

    /// For QML: one map per account with name, flags, and usage rows.
    QVariantList accountsModel() const;

    // Account list
    Q_INVOKABLE QString adoptCurrentLogin(const QString &name);
    Q_INVOKABLE QString addAccount(const QString &name);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void setEmail(const QString &id, const QString &email);
    Q_INVOKABLE void move(const QString &id, int offset);
    /// Moves the account's data to the Trash. Throws via the error signal.
    Q_INVOKABLE bool remove(const QString &id);

    // Switching. Each runs in the background and ends with `finished` or `error`.
    Q_INVOKABLE void switchTo(const QString &id);
    Q_INVOKABLE void showSideBySide(const QString &id);
    Q_INVOKABLE void setSideBySide(bool on);
    Q_INVOKABLE void setShareSessions(bool on);
    Q_INVOKABLE void syncSessionsNow();
    Q_INVOKABLE void openClaude();
    Q_INVOKABLE void setAutoSwitch(const QString &modeKey);
    Q_INVOKABLE void clearAutoSwitchNote();

    // Usage
    Q_INVOKABLE void refreshUsage();
    /// Called by the live usage reader (app layer) with the result of one account.
    void setLiveResult(const QString &id, const std::optional<Usage> &usage, LiveState state, const QString &detail = {});
    void setLoading(const QString &id, bool loading);

    /// Puts the session folder of every account with NO open Claude copy in the wanted state.
    Q_INVOKABLE void tidyIdleFolders();
    Q_INVOKABLE void refreshRunning();

Q_SIGNALS:
    void changed();
    void error(const QString &message);
    /// The app layer reads live usage for these accounts (hidden web views).
    void liveRefreshRequested(const QStringList &ids);
    /// Mode "ask": the app shows a question, then calls switchTo(targetID) on yes.
    void autoSwitchProposed(const QString &currentName, const QString &targetID, const QString &targetName,
                            const QString &reason, int targetPercent);

private:
    void load();
    void save() const;
    /// Runs `job` off the GUI thread with isSwitching set; `done` runs on the GUI thread on success.
    void runJob(std::function<void()> job, std::function<void()> done = {});
    /// Quits all copies, runs `change`, re-applies every session folder, reopens if it was open.
    void applySessionLayout(std::function<void()> change);
    void checkAutoSwitch();

    Paths m_paths;
    ClaudeProcess m_claude;
    QList<Account> m_accounts;
    QString m_activeID;
    bool m_switching = false;
    bool m_sideBySide = false;
    bool m_shareSessions = true;
    bool m_shareInSideBySide = false;
    AutoSwitch::Mode m_autoSwitch = AutoSwitch::Mode::Off;
    QString m_autoSwitchNote;
    QDateTime m_autoSwitchPausedUntil;
    QSet<QString> m_running;
    QSet<QString> m_loading;
    QHash<QString, Usage> m_usage;
    QHash<QString, Usage> m_liveUsage;
    QHash<QString, LiveState> m_liveState;
    QDateTime m_lastLiveFetch;
    QTimer m_usageTimer;
    QTimer m_runningTimer;
};

} // namespace clauds
