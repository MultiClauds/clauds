// SPDX-License-Identifier: MIT
#pragma once

#include "Account.h"
#include "Usage.h"

#include <QHash>
#include <optional>

namespace clauds::AutoSwitch {

enum class Mode { Off, Ask, Auto };

QString modeKey(Mode m);          ///< "off" | "ask" | "auto" (same values as the macOS accounts.json)
Mode modeFromKey(const QString &key);

/// The current account is "near a limit" at this percent or more.
constexpr int threshold = 90;
/// The other account must be this many points below the threshold.
constexpr int margin = 10;
/// Numbers older than this (seconds) are not used for a decision.
constexpr qint64 maxAge = 15 * 60;
/// Seconds between two questions or two automatic switches.
constexpr qint64 pause = 20 * 60;

struct Proposal {
    Account target;
    Usage::Limit limit;   ///< the limit of the current account that is near its end
    int targetPercent = 0;
};

std::optional<Usage::Limit> fullest(const Usage &usage, const QDateTime &now);

std::optional<Proposal> proposal(const QList<Account> &accounts, const QString &activeID,
                                 const QHash<QString, Usage> &usage,
                                 const QDateTime &now = QDateTime::currentDateTimeUtc());

} // namespace clauds::AutoSwitch
