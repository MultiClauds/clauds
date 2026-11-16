// SPDX-License-Identifier: MIT
#include "SessionLinker.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace clauds::SessionLinker {

namespace {

std::recursive_mutex &lock()
{
    static std::recursive_mutex m;
    return m;
}

enum class Kind { Folder, Symlink, File };

Kind kind(const Path &p)
{
    std::error_code ec;
    const auto st = fs::symlink_status(p, ec);
    if (fs::is_symlink(st))
        return Kind::Symlink;
    return fs::is_directory(st) ? Kind::Folder : Kind::File;
}

QString qs(const Path &p) { return QString::fromStdString(p.string()); }

std::vector<Path> children(const Path &dir)
{
    std::vector<Path> out;
    for (const auto &entry : fs::directory_iterator(dir)) {
        const auto name = entry.path().filename().string();
        if (name == ".DS_Store" || name == ".directory")   // Finder / Dolphin metadata
            continue;
        out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end(), [](const Path &a, const Path &b) { return a.filename() < b.filename(); });
    return out;
}

/// True if the symlink's target is exactly `target`.
bool pointsTo(const Path &symlink, const Path &target)
{
    std::error_code ec;
    auto dest = fs::read_symlink(symlink, ec);
    if (ec)
        return false;
    if (dest.is_relative())
        dest = symlink.parent_path() / dest;
    return dest.lexically_normal() == target.lexically_normal();
}

void makePrivateDir(const Path &dir)
{
    fs::create_directories(dir);
    std::error_code ec;
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
}

void removeIfEmpty(const Path &dir)
{
    if (!children(dir).empty())
        throw Error("A folder is not empty: " + dir.string());
    // .DS_Store / .directory do not count, so remove them first.
    for (const auto &entry : fs::directory_iterator(dir))
        fs::remove(entry.path());
    fs::remove(dir);
}

bool contentsEqual(const Path &a, const Path &b)
{
    if (kind(a) != Kind::File || kind(b) != Kind::File)
        return false;
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec) || ec)
        return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(fb));
}

fs::file_time_type modified(const Path &p)
{
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : t;
}

/// Moves `item` to `dest`. If `dest` exists with different content, the newer one wins and the
/// older one moves to "<shared>/_conflicts/<label>/<time>_<name>". Nothing is deleted, except a
/// file that is an exact copy of the one that stays.
void moveKeepingNewer(const Path &item, const Path &dest, const Path &sharedDir, const QString &label)
{
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(dest, ec))) {
        fs::rename(item, dest);
        return;
    }
    if (contentsEqual(item, dest)) {
        fs::remove(item);   // exact copy
        return;
    }
    const Path conflicts = sharedDir / "_conflicts" / label.toStdString();
    makePrivateDir(conflicts);
    const auto stamp = QDateTime::currentMSecsSinceEpoch();
    const Path loserSpot = conflicts / (std::to_string(stamp) + "_" + item.filename().string());
    if (modified(item) > modified(dest)) {
        fs::rename(dest, loserSpot);
        fs::rename(item, dest);
    } else {
        fs::rename(item, loserSpot);
    }
}

void merge(const Path &source, const Path &dest, const Path &sharedDir, const QString &label)
{
    for (const auto &item : children(source))
        moveKeepingNewer(item, dest / item.filename(), sharedDir, label);
}

/// Old layout only: session files loose in <shared> go to one org folder.
void migrateLooseFiles(const Path &sharedDir, const Path &orgDir)
{
    static const std::vector<std::string> keep = {"orgs", "_conflicts", "owners.json", "owners.json.bak"};
    for (const auto &item : children(sharedDir)) {
        if (std::find(keep.begin(), keep.end(), item.filename().string()) != keep.end())
            continue;
        moveKeepingNewer(item, orgDir / item.filename(), sharedDir, QStringLiteral("migrated"));
    }
}

void writePrivate(const Path &file, const QByteArray &data)
{
    QSaveFile out(qs(file));
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(data);
    if (out.commit()) {
        QFile::setPermissions(qs(file), QFile::ReadOwner | QFile::WriteOwner);
    }
}

/// Claude stores "archived" in two places: the isArchived mark inside each session file, and one
/// list per folder (archived-sessions.idx). Session files move, so the list is rebuilt from the marks.
void rebuildArchiveList(const Path &orgDir)
{
    const Path file = orgDir / "archived-sessions.idx";
    QJsonObject root{{QStringLiteral("v"), 1}};
    QStringList old;
    if (fs::exists(file)) {
        QFile f(qs(file));
        if (!f.open(QIODevice::ReadOnly))
            return;   // cannot be read: leave it as it is
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject())
            return;
        root = doc.object();
        for (const auto &v : root.value(QStringLiteral("archived")).toArray())
            old << v.toString();
    }
    std::vector<Path> items;
    try {
        items = children(orgDir);
    } catch (...) {
        return;
    }
    QStringList archived;
    for (const auto &item : items) {
        const QString id = sessionID(item);
        if (id.isEmpty())
            continue;
        QFile f(qs(item));
        QJsonParseError err{};
        const auto doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &err) : QJsonDocument();
        if (doc.isObject()) {
            if (doc.object().value(QStringLiteral("isArchived")).toBool(false))
                archived << id;
        } else if (old.contains(id)) {
            archived << id;   // session file cannot be read: keep its old state
        }
    }
    archived.sort();
    if (archived == old)
        return;
    root[QStringLiteral("archived")] = QJsonArray::fromStringList(archived);
    writePrivate(file, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void rebuildArchiveLists(const Path &parent)
{
    for (const auto &orgDir : children(parent))
        if (kind(orgDir) == Kind::Folder)
            rebuildArchiveList(orgDir);
}

/// Saves the owner record when this scope ends (like Swift's `defer`), also after an error.
struct OwnersGuard {
    Owners &owners;
    const Path &sharedDir;
    ~OwnersGuard() { saveOwners(owners, sharedDir); }
};

} // namespace

Path ownersFile(const Path &sharedDir) { return sharedDir / "owners.json"; }

QString signedInKey(const QString &accountTag) { return QStringLiteral("@account-of:") + accountTag; }

Owners loadOwners(const Path &sharedDir)
{
    const Path file = ownersFile(sharedDir);
    if (!fs::exists(file))
        return {};
    QFile f(qs(file));
    if (!f.open(QIODevice::ReadOnly))
        throw Error("The session owner record cannot be read: " + file.string()
                    + ". Nothing was changed. A copy is next to it (owners.json.bak).");
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        throw Error("The session owner record cannot be read: " + file.string()
                    + ". Nothing was changed. A copy is next to it (owners.json.bak).");
    Owners owners;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!it.value().isString())
            throw Error("The session owner record cannot be read: " + file.string());
        owners.insert(it.key(), it.value().toString());
    }
    return owners;
}

void saveOwners(const Owners &owners, const Path &sharedDir)
{
    const Path file = ownersFile(sharedDir);
    Owners old;
    try {
        old = loadOwners(sharedDir);
    } catch (...) {
        return;   // never overwrite a record that cannot be read
    }
    if (old == owners && fs::exists(file))
        return;
    if (old == owners && owners.isEmpty())
        return;
    QJsonObject obj;
    for (auto it = owners.begin(); it != owners.end(); ++it)
        obj.insert(it.key(), it.value());
    if (fs::exists(file)) {
        const Path backup = sharedDir / "owners.json.bak";
        std::error_code ec;
        fs::copy_file(file, backup, fs::copy_options::overwrite_existing, ec);
    }
    writePrivate(file, QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QString sessionID(const Path &file)
{
    const QString name = QString::fromStdString(file.filename().string());
    if (!name.startsWith(QLatin1String("local_")) || !name.endsWith(QLatin1String(".json")))
        return {};
    return name.chopped(5);
}

int link(const Path &claudeDir, const Path &sharedDir, const std::optional<QString> &accountTag)
{
    std::lock_guard guard(lock());
    const Path sessionsRoot = claudeDir / "claude-code-sessions";
    if (!fs::exists(sessionsRoot))
        return 0;
    const Path orgsRoot = sharedDir / "orgs";
    makePrivateDir(orgsRoot);

    Owners owners = loadOwners(sharedDir);
    OwnersGuard save{owners, sharedDir};
    int linked = 0;
    for (const auto &accountDir : children(sessionsRoot)) {
        if (kind(accountDir) != Kind::Folder)
            continue;
        const QString account = qs(accountDir.filename());
        for (const auto &item : children(accountDir)) {
            const QString label = account + QLatin1Char('_') + qs(item.filename());
            switch (kind(item)) {
            case Kind::Symlink: {
                // Old layout of the app: <org id> -> <shared>. Its files are loose in <shared>.
                if (!pointsTo(item, sharedDir))
                    continue;   // not ours: leave it
                const Path orgDir = orgsRoot / item.filename();
                makePrivateDir(orgDir);
                migrateLooseFiles(sharedDir, orgDir);
                fs::remove(item);   // the symlink only
                break;
            }
            case Kind::Folder: {
                const Path orgDir = orgsRoot / item.filename();
                makePrivateDir(orgDir);
                // A session with no owner record was made in this account's folder.
                for (const auto &file : children(item)) {
                    const QString id = sessionID(file);
                    if (!id.isEmpty() && !owners.contains(id)) {
                        owners.insert(id, account);
                        if (accountTag)
                            owners.insert(signedInKey(*accountTag), account);
                    }
                }
                merge(item, orgDir, sharedDir, label);
                removeIfEmpty(item);
                break;
            }
            case Kind::File:
                moveKeepingNewer(item, orgsRoot / item.filename(), sharedDir, label);
                break;
            }
        }
        removeIfEmpty(accountDir);
        fs::create_directory_symlink(orgsRoot, accountDir);
        ++linked;
    }
    rebuildArchiveLists(orgsRoot);
    return linked;
}

namespace {

/// Own session files MOVE from the pool to the account's folder. Other items are copied, only if
/// the account does not have them.
template<typename IsMine>
void takeOwnFiles(const Path &poolOrg, const Path &orgDir, const QString &account, IsMine isMine, const Path &sharedDir)
{
    for (const auto &item : children(poolOrg)) {
        const Path dest = orgDir / item.filename();
        const QString id = sessionID(item);
        if (!id.isEmpty()) {
            if (!isMine(id, account))
                continue;
            moveKeepingNewer(item, dest, sharedDir, QStringLiteral("pool_") + account);
        } else if (!fs::exists(fs::symlink_status(dest))) {
            fs::copy(item, dest, fs::copy_options::recursive);
        }
    }
}

} // namespace

int unlink(const Path &claudeDir, const Path &sharedDir, const std::optional<QString> &accountTag)
{
    std::lock_guard guard(lock());
    const Path sessionsRoot = claudeDir / "claude-code-sessions";
    if (!fs::exists(sessionsRoot))
        return 0;
    const Path orgsRoot = sharedDir / "orgs";
    makePrivateDir(orgsRoot);

    Owners owners = loadOwners(sharedDir);
    OwnersGuard save{owners, sharedDir};
    // Sessions made while shared carry the Clauds tag. Change it to the signed-in Claude account.
    if (accountTag) {
        const QString signedIn = owners.value(signedInKey(*accountTag));
        if (!signedIn.isEmpty()) {
            for (auto it = owners.begin(); it != owners.end(); ++it)
                if (it.value() == *accountTag)
                    it.value() = signedIn;
        }
    }
    auto isMine = [&owners, &accountTag](const QString &id, const QString &account) {
        const auto it = owners.constFind(id);
        if (it == owners.constEnd())
            return false;
        return it.value() == account || (accountTag && it.value() == *accountTag);
    };

    int unlinked = 0;
    for (const auto &accountDir : children(sessionsRoot)) {
        const QString account = qs(accountDir.filename());
        switch (kind(accountDir)) {
        case Kind::Symlink:
            if (!pointsTo(accountDir, orgsRoot))
                continue;   // not ours: leave it
            fs::remove(accountDir);   // the symlink only
            makePrivateDir(accountDir);
            for (const auto &poolOrg : children(orgsRoot)) {
                if (kind(poolOrg) != Kind::Folder)
                    continue;
                const Path orgDir = accountDir / poolOrg.filename();
                makePrivateDir(orgDir);
                takeOwnFiles(poolOrg, orgDir, account, isMine, sharedDir);
            }
            ++unlinked;
            break;
        case Kind::Folder:
            for (const auto &orgDir : children(accountDir)) {
                if (kind(orgDir) != Kind::Folder)
                    continue;
                const Path poolOrg = orgsRoot / orgDir.filename();
                makePrivateDir(poolOrg);
                for (const auto &item : children(orgDir)) {
                    const QString id = sessionID(item);
                    if (id.isEmpty())
                        continue;
                    if (!owners.contains(id)) {
                        owners.insert(id, account);
                        if (accountTag)
                            owners.insert(signedInKey(*accountTag), account);
                    } else if (!isMine(id, account)) {
                        moveKeepingNewer(item, poolOrg / item.filename(), sharedDir, QStringLiteral("not-owner_") + account);
                    }
                }
                takeOwnFiles(poolOrg, orgDir, account, isMine, sharedDir);
            }
            break;
        case Kind::File:
            break;
        }
    }
    rebuildArchiveLists(orgsRoot);
    for (const auto &accountDir : children(sessionsRoot))
        if (kind(accountDir) == Kind::Folder)
            rebuildArchiveLists(accountDir);
    return unlinked;
}

void apply(const Path &claudeDir, const Path &sharedDir, bool share, const std::optional<QString> &accountTag)
{
    if (share)
        link(claudeDir, sharedDir, accountTag);
    else
        unlink(claudeDir, sharedDir, accountTag);
}

void claimUnknown(const Path &sharedDir, const QString &tag)
{
    std::lock_guard guard(lock());
    const Path orgsRoot = sharedDir / "orgs";
    if (!fs::exists(orgsRoot))
        return;
    Owners owners = loadOwners(sharedDir);
    for (const auto &poolOrg : children(orgsRoot)) {
        if (kind(poolOrg) != Kind::Folder)
            continue;
        for (const auto &item : children(poolOrg)) {
            const QString id = sessionID(item);
            if (!id.isEmpty() && !owners.contains(id))
                owners.insert(id, tag);
        }
    }
    saveOwners(owners, sharedDir);
}

} // namespace clauds::SessionLinker
