// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

#include <filesystem>
#include <optional>
#include <stdexcept>

namespace clauds {

/// Controls where the Claude app's Code session files are, and which account sees them.
///
/// The Claude app keeps one small file per Code session here:
///   <Claude data>/claude-code-sessions/<account id>/<org id>/local_<session id>.json
/// The conversation itself is in ~/.claude/projects, which all accounts already share.
///
/// SHARED layout (link):
///   <shared>/orgs/<org id>/                          real folder, holds ALL session files
///   <Claude data>/claude-code-sessions/<account id>  ->  symlink to <shared>/orgs
/// The symlink MUST be at the account level. Claude opens the <org id> folder with
/// O_NOFOLLOW, so a symlink at the org level makes every save fail.
///
/// SEPARATE layout (unlink):
///   <Claude data>/claude-code-sessions/<account id>/<org id>/   real folder, holds ONLY the
///   sessions that this account owns. Sessions of other accounts wait in <shared>/orgs.
///
/// Data safety rules (same as the macOS app, see docs/HOW-IT-WORKS.md):
///   1. One live file per session. Session files MOVE; they are not copied.
///   2. Nothing is deleted. Same name, different content: the newer wins, the older goes to
///      <shared>/_conflicts.
///   3. A folder is removed only when it is empty.
///   4. If the owner record cannot be read, every operation stops.
///   5. One operation at a time (lock).
///   6. A symlink that this app did not make is not touched.
namespace SessionLinker {

using Path = std::filesystem::path;
using Owners = QMap<QString, QString>;

class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Shared layout. Returns the number of account folders linked in this call.
int link(const Path &claudeDir, const Path &sharedDir, const std::optional<QString> &accountTag = std::nullopt);

/// Separate layout. Returns the number of account folders unlinked in this call.
int unlink(const Path &claudeDir, const Path &sharedDir, const std::optional<QString> &accountTag = std::nullopt);

/// Puts one Claude data folder in the wanted state.
void apply(const Path &claudeDir, const Path &sharedDir, bool share, const std::optional<QString> &accountTag = std::nullopt);

/// Gives every pool session with no owner record to `tag`.
void claimUnknown(const Path &sharedDir, const QString &tag);

Path ownersFile(const Path &sharedDir);
QString signedInKey(const QString &accountTag);
/// Empty map when no file. Throws Error when the file exists but cannot be read.
Owners loadOwners(const Path &sharedDir);
void saveOwners(const Owners &owners, const Path &sharedDir);
/// "local_<id>.json" -> "local_<id>". Empty when not a session file.
QString sessionID(const Path &file);

} // namespace SessionLinker
} // namespace clauds
