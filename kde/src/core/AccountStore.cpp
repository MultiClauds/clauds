// SPDX-License-Identifier: MIT
#include "AccountStore.h"
#include "SessionLinker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace clauds {

namespace {

class SwitchError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

fs::path P(const QString &s) { return fs::path(s.toStdString()); }

void makePrivate(const QString &dir)
{
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

/// Same-disk rename. A rename between two disks would copy gigabytes; refuse that instead.
void renameFolder(const QString &from, const QString &to)
{
    std::error_code ec;
    fs::rename(P(from), P(to), ec);
    if (ec == std::errc::cross_device_link)
        throw SwitchError(QStringLiteral("%1 and %2 are on different disks. Clauds needs them on the same disk.")
                              .arg(from, to).toStdString());
    if (ec)
        throw SwitchError(QStringLiteral("Could not move %1 to %2: %3")
                              .arg(from, to, QString::fromStdString(ec.message())).toStdString());
}

} // namespace

AccountStore::Paths AccountStore::Paths::standard()
{
    Paths p;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    p.claudeDir = qEnvironmentVariable("CLAUDS_CLAUDE_DATA_DIR", config + QStringLiteral("/Claude"));
    p.supportDir = qEnvironmentVariable("CLAUDS_DATA_DIR", data + QStringLiteral("/clauds"));
    return p;
}

AccountStore::AccountStore(const Paths &paths, ClaudeProcess claude, QObject *parent)
    : QObject(parent)
    , m_paths(paths)
    , m_claude(std::move(claude))
{
    for (const auto &dir : {m_paths.supportDir, profilesDir(), sharedSessionsDir()})
        makePrivate(dir);
    load();

    // Linux has no "app launched / quit" notification for other apps: look every 3 s.
    m_runningTimer.setInterval(3000);
    connect(&m_runningTimer, &QTimer::timeout, this, [this] {
        const auto before = m_running;
        refreshRunning();
        if (before != m_running) {
            // A Claude copy quit or started. Wait for its helpers, then fix idle folders.
            QTimer::singleShot(4000, this, &AccountStore::tidyIdleFolders);
        }
    });
    m_runningTimer.start();
    refreshRunning();
    tidyIdleFolders();

    // Usage numbers every 2 minutes, also while the panel is closed.
    m_usageTimer.setInterval(120 * 1000);
    connect(&m_usageTimer, &QTimer::timeout, this, &AccountStore::refreshUsage);
    m_usageTimer.start();
    QTimer::singleShot(0, this, &AccountStore::refreshUsage);
}

AccountStore::~AccountStore() = default;

std::optional<Account> AccountStore::account(const QString &id) const
{
    for (const auto &a : m_accounts)
        if (a.id == id)
            return a;
    return std::nullopt;
}

// MARK: Persistence

void AccountStore::load()
{
    QFile f(m_paths.supportDir + QStringLiteral("/accounts.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const auto o = QJsonDocument::fromJson(f.readAll()).object();
    m_accounts.clear();
    for (const auto &v : o.value(QStringLiteral("accounts")).toArray())
        m_accounts << Account::fromJson(v.toObject());
    m_activeID = o.value(QStringLiteral("activeID")).toString();
    m_sideBySide = o.value(QStringLiteral("sideBySide")).toBool(false);
    m_shareSessions = o.value(QStringLiteral("shareSessions")).toBool(true);
    m_autoSwitch = AutoSwitch::modeFromKey(o.value(QStringLiteral("autoSwitch")).toString());
    m_shareInSideBySide = o.value(QStringLiteral("shareInSideBySide")).toBool(false);
}

void AccountStore::save() const
{
    QJsonArray list;
    for (const auto &a : m_accounts)
        list << a.toJson();
    QJsonObject o{{QStringLiteral("accounts"), list},
                  {QStringLiteral("sideBySide"), m_sideBySide},
                  {QStringLiteral("shareSessions"), m_shareSessions},
                  {QStringLiteral("shareInSideBySide"), m_shareInSideBySide},
                  {QStringLiteral("autoSwitch"), AutoSwitch::modeKey(m_autoSwitch)}};
    if (!m_activeID.isEmpty())
        o.insert(QStringLiteral("activeID"), m_activeID);
    const QString path = m_paths.supportDir + QStringLiteral("/accounts.json");
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (out.commit())
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
}

// MARK: Account list

QString AccountStore::adoptCurrentLogin(const QString &name)
{
    const Account a{QUuid::createUuid().toString(QUuid::WithoutBraces), name, {}};
    m_accounts << a;
    m_activeID = a.id;
    save();
    Q_EMIT changed();
    return a.id;
}

QString AccountStore::addAccount(const QString &name)
{
    const Account a{QUuid::createUuid().toString(QUuid::WithoutBraces), name, {}};
    m_accounts << a;
    if (m_activeID.isEmpty())
        m_activeID = a.id;
    save();
    Q_EMIT changed();
    return a.id;
}

void AccountStore::rename(const QString &id, const QString &name)
{
    for (auto &a : m_accounts)
        if (a.id == id)
            a.name = name;
    save();
    Q_EMIT changed();
}

void AccountStore::setEmail(const QString &id, const QString &email)
{
    for (auto &a : m_accounts)
        if (a.id == id)
            a.email = email;
    save();
    Q_EMIT changed();
}

void AccountStore::move(const QString &id, int offset)
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i].id != id)
            continue;
        const int target = i + offset;
        if (target < 0 || target >= m_accounts.size())
            return;
        m_accounts.swapItemsAt(i, target);
        save();
        Q_EMIT changed();
        return;
    }
}

bool AccountStore::remove(const QString &id)
{
    if (id == m_activeID) {
        Q_EMIT error(QStringLiteral("You cannot remove the active account. Switch to a different account first."));
        return false;
    }
    if (!m_claude.mainsFor(profileDir(id)).isEmpty()) {
        Q_EMIT error(QStringLiteral("This account has a Claude window open. Quit that Claude first."));
        return false;
    }
    const QString dir = profileDir(id);
    if (QFileInfo::exists(dir)) {
        try {
            // Safety: first move this account's Code sessions to the shared pool, so they are not lost.
            SessionLinker::link(P(dir), P(sharedSessionsDir()), Account::tag(id));
        } catch (const std::exception &e) {
            Q_EMIT error(QString::fromStdString(e.what()));
            return false;
        }
        if (!QFile::moveToTrash(dir)) {
            Q_EMIT error(QStringLiteral("Could not move %1 to the Trash. Nothing was removed.").arg(dir));
            return false;
        }
    }
    m_accounts.removeIf([&id](const Account &a) { return a.id == id; });
    m_usage.remove(id);
    m_liveUsage.remove(id);
    save();
    Q_EMIT changed();
    return true;
}

// MARK: Background jobs

void AccountStore::runJob(std::function<void()> job, std::function<void()> done)
{
    if (m_switching)
        return;
    m_switching = true;
    Q_EMIT changed();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, done] {
        const QString failure = watcher->result();
        watcher->deleteLater();
        m_switching = false;
        if (failure.isEmpty() && done)
            done();
        refreshRunning();
        Q_EMIT changed();
        if (!failure.isEmpty())
            Q_EMIT error(failure);
    });
    watcher->setFuture(QtConcurrent::run([job]() -> QString {
        try {
            job();
            return {};
        } catch (const std::exception &e) {
            return QString::fromStdString(e.what());
        }
    }));
}

// MARK: Switching (restart mode)

void AccountStore::switchTo(const QString &id)
{
    if (id == m_activeID || m_switching || !account(id))
        return;
    const QString current = m_activeID;
    const QString claudeDir = this->claudeDir();
    const QString targetDir = profileDir(id);
    const QString parkDir = current.isEmpty() ? QString() : profileDir(current);
    const QString sharedDir = sharedSessionsDir();
    const bool share = sessionsAreShared();
    const std::optional<QString> currentTag = current.isEmpty() ? std::nullopt : std::optional(Account::tag(current));
    const QString targetTag = Account::tag(id);
    const ClaudeProcess claude = m_claude;

    runJob(
        [=] {
            if (!claude.quitAll())
                throw SwitchError("Claude did not quit. Close Claude yourself, then try again.");

            // 0. The outgoing account made the new shared sessions. Then put its sessions in the wanted state.
            if (share && currentTag)
                SessionLinker::claimUnknown(P(sharedDir), *currentTag);
            SessionLinker::apply(P(claudeDir), P(sharedDir), share, currentTag);

            // 1. Park the active account's data.
            bool parked = false;
            if (QFileInfo::exists(claudeDir)) {
                if (parkDir.isEmpty())
                    throw SwitchError("Claude data has no account. Use \"Save Current Claude Login as Account\" first.");
                if (QFileInfo::exists(parkDir))
                    throw SwitchError(QStringLiteral("A folder is already at %1. Nothing was changed.").arg(parkDir).toStdString());
                renameFolder(claudeDir, parkDir);
                parked = true;
            }

            // 2. Bring the target's data in. A new account has no data: Claude starts at sign-in.
            if (QFileInfo::exists(targetDir)) {
                try {
                    renameFolder(targetDir, claudeDir);
                } catch (...) {
                    // Undo step 1 so Claude still opens with the old account.
                    if (parked) {
                        std::error_code ec;
                        fs::rename(P(parkDir), P(claudeDir), ec);
                    }
                    throw;
                }
                // 3. The same for the incoming account.
                SessionLinker::apply(P(claudeDir), P(sharedDir), share, targetTag);
            }
        },
        [this, id] {
            m_activeID = id;
            save();
            openClaude();
        });
}

void AccountStore::applySessionLayout(std::function<void()> change)
{
    if (m_switching)
        return;
    const bool wasRunning = isClaudeRunning();
    const bool wasShared = sessionsAreShared();
    const ClaudeProcess claude = m_claude;
    // The quit must happen before the setting changes, so a failed quit changes nothing.
    // The quit runs in the job; the setting change and the folder work follow in a second job.
    runJob([claude] {
        if (!claude.quitAll())
            throw SwitchError("Claude did not quit. Close Claude yourself, then try again.");
    }, [this, change, wasRunning, wasShared] {
        change();
        save();
        Q_EMIT changed();
        const std::optional<QString> activeTag = m_activeID.isEmpty() ? std::nullopt : std::optional(Account::tag(m_activeID));
        QList<QPair<QString, std::optional<QString>>> dirs{{claudeDir(), activeTag}};
        for (const auto &a : m_accounts)
            dirs.append({profileDir(a.id), Account::tag(a.id)});
        const QString sharedDir = sharedSessionsDir();
        const bool share = sessionsAreShared();
        // Deferred, so that the first job's flag is cleared before the second job starts.
        QTimer::singleShot(0, this, [=, this] {
            runJob([=] {
                // Sessions made while shared belong to the account that ran then (the main account).
                if (wasShared && activeTag)
                    SessionLinker::claimUnknown(P(sharedDir), *activeTag);
                for (const auto &[dir, tag] : dirs)
                    SessionLinker::apply(P(dir), P(sharedDir), share, tag);
            }, [this, wasRunning] {
                if (wasRunning)
                    openClaude();
            });
        });
    });
}

void AccountStore::tidyIdleFolders()
{
    if (m_switching)
        return;
    QList<QPair<QString, QString>> idle;
    for (const auto &a : m_accounts) {
        const bool open = a.id == m_activeID ? !m_claude.mainsFor({}).isEmpty() : !m_claude.mainsFor(profileDir(a.id)).isEmpty();
        if (!open)
            idle.append({dataDir(a.id), Account::tag(a.id)});
    }
    const QString sharedDir = sharedSessionsDir();
    const bool share = sessionsAreShared();
    (void)QtConcurrent::run([=] {
        for (const auto &[dir, tag] : idle) {
            try {
                SessionLinker::apply(P(dir), P(sharedDir), share, tag);
            } catch (...) {
            }
        }
    });
}

void AccountStore::syncSessionsNow() { applySessionLayout([] {}); }

void AccountStore::setShareSessions(bool on)
{
    applySessionLayout([this, on] {
        if (m_sideBySide) {
            m_shareInSideBySide = on;
            if (on)
                m_shareSessions = true;
        } else {
            m_shareSessions = on;
        }
    });
}

// MARK: Side by Side

void AccountStore::setSideBySide(bool on)
{
    applySessionLayout([this, on] {
        m_sideBySide = on;
        if (!on)
            m_shareSessions = true;
    });
}

void AccountStore::refreshRunning()
{
    QSet<QString> running;
    const auto mains = m_claude.mains();
    for (const auto &a : m_accounts) {
        const QString want = a.id == m_activeID ? QString() : QDir::cleanPath(profileDir(a.id));
        for (const auto &p : mains)
            if (p.userDataDir() == want)
                running.insert(a.id);
    }
    if (running != m_running) {
        m_running = running;
        Q_EMIT changed();
    }
}

void AccountStore::showSideBySide(const QString &id)
{
    if (!account(id))
        return;
    if (!m_claude.isInstalled()) {
        Q_EMIT error(QStringLiteral("The Claude app was not found. Install the claude-desktop package."));
        return;
    }
    const bool isMain = id == m_activeID;
    const QString dir = dataDir(id);
    if (m_running.contains(id)) {
        // A second start with the same data folder hands over to the open copy (Electron
        // single-instance lock), which shows its window.
        m_claude.launch(isMain ? QString() : dir);
        return;
    }
    const QString sharedDir = sharedSessionsDir();
    const QString tag = Account::tag(id);
    const bool share = sessionsAreShared();
    runJob([=] {
        if (!isMain)
            makePrivate(dir);
        // This account's copy is not open, so the change of its folder is safe.
        SessionLinker::apply(P(dir), P(sharedDir), share, tag);
    }, [this, isMain, dir] { m_claude.launch(isMain ? QString() : dir); });
}

void AccountStore::openClaude()
{
    if (!m_claude.launch())
        Q_EMIT error(QStringLiteral("The Claude app was not found. Install the claude-desktop package."));
}

// MARK: Usage

void AccountStore::refreshUsage()
{
    const auto now = QDateTime::currentDateTimeUtc();
    QHash<QString, Usage> result;
    for (const auto &a : m_accounts) {
        const auto local = Usage::readLocal(dataDir(a.id), now);
        const auto live = m_liveUsage.constFind(a.id);
        // Live numbers have the reset times. Keep them while they are not older than 10 minutes.
        if (live != m_liveUsage.constEnd()
            && (!local || live->sampledAt >= local->sampledAt || live->sampledAt.secsTo(now) < 600))
            result.insert(a.id, *live);
        else if (local)
            result.insert(a.id, *local);
    }
    if (result != m_usage) {
        m_usage = result;
        Q_EMIT changed();
    }
    // Not more than one live read in 45 s.
    if (!m_lastLiveFetch.isValid() || m_lastLiveFetch.secsTo(now) > 45) {
        m_lastLiveFetch = now;
        QStringList ids;
        for (const auto &a : m_accounts)
            ids << a.id;
        if (!ids.isEmpty())
            Q_EMIT liveRefreshRequested(ids);
    }
}

void AccountStore::setLoading(const QString &id, bool loading)
{
    if (loading)
        m_loading.insert(id);
    else
        m_loading.remove(id);
    Q_EMIT changed();
}

void AccountStore::setLiveResult(const QString &id, const std::optional<Usage> &usage, LiveState state, const QString &)
{
    m_loading.remove(id);
    m_liveState.insert(id, state);
    if (usage) {
        m_liveUsage.insert(id, *usage);
        if (!m_usage.contains(id) || usage->sampledAt >= m_usage.value(id).sampledAt)
            m_usage.insert(id, *usage);
    }
    Q_EMIT changed();
    checkAutoSwitch();
}

// MARK: Auto-switch

void AccountStore::setAutoSwitch(const QString &modeKey)
{
    m_autoSwitch = AutoSwitch::modeFromKey(modeKey);
    m_autoSwitchPausedUntil = {};
    save();
    Q_EMIT changed();
}

void AccountStore::clearAutoSwitchNote()
{
    m_autoSwitchNote.clear();
    Q_EMIT changed();
}

void AccountStore::checkAutoSwitch()
{
    const auto now = QDateTime::currentDateTimeUtc();
    if (m_autoSwitch == AutoSwitch::Mode::Off || m_sideBySide || m_switching || !isClaudeRunning())
        return;
    if (m_autoSwitchPausedUntil.isValid() && now < m_autoSwitchPausedUntil)
        return;
    const auto current = account(m_activeID);
    const auto plan = AutoSwitch::proposal(m_accounts, m_activeID, m_usage, now);
    if (!current || !plan)
        return;
    m_autoSwitchPausedUntil = now.addSecs(AutoSwitch::pause);
    const QString reason = QStringLiteral("%1: %2% used.").arg(plan->limit.fullName).arg(plan->limit.currentPercent(now));
    if (m_autoSwitch == AutoSwitch::Mode::Ask) {
        Q_EMIT autoSwitchProposed(current->name, plan->target.id, plan->target.name, reason, plan->targetPercent);
        return;
    }
    m_autoSwitchNote = QStringLiteral("Switched to %1. %2 was near a limit. %3").arg(plan->target.name, current->name, reason);
    switchTo(plan->target.id);
    Q_EMIT autoSwitched(m_autoSwitchNote);
}

// MARK: QML model

QVariantList AccountStore::accountsModel() const
{
    const auto now = QDateTime::currentDateTimeUtc();
    QVariantList list;
    for (int i = 0; i < m_accounts.size(); ++i) {
        const auto &a = m_accounts[i];
        QVariantMap m;
        m[QStringLiteral("id")] = a.id;
        m[QStringLiteral("name")] = a.name;
        m[QStringLiteral("email")] = a.email;
        m[QStringLiteral("isActive")] = a.id == m_activeID;
        m[QStringLiteral("isRunning")] = m_running.contains(a.id);
        m[QStringLiteral("isLoading")] = m_loading.contains(a.id);
        m[QStringLiteral("isFirst")] = i == 0;
        m[QStringLiteral("isLast")] = i == m_accounts.size() - 1;
        const LiveState st = m_liveState.value(a.id, LiveState::Unknown);
        m[QStringLiteral("needsSignIn")] = st == LiveState::NeedsSignIn;
        m[QStringLiteral("liveFailed")] = st == LiveState::Failed;
        QVariantList limits;
        if (m_usage.contains(a.id)) {
            const Usage &u = m_usage[a.id];
            for (const auto &l : u.limits) {
                const int p = l.currentPercent(now);
                limits << QVariantMap{{QStringLiteral("label"), l.label},
                                      {QStringLiteral("percent"), p},
                                      {QStringLiteral("level"), p < 60 ? 0 : (p < 85 ? 1 : 2)},
                                      {QStringLiteral("resetText"), l.resetText(now)},
                                      {QStringLiteral("tip"), l.tip(now)}};
            }
            m[QStringLiteral("age")] = u.age(now);
            m[QStringLiteral("isLive")] = u.isLive;
        }
        m[QStringLiteral("limits")] = limits;
        list << m;
    }
    return list;
}

} // namespace clauds
