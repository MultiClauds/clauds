// SPDX-License-Identifier: MIT
#include "AutoSwitch.h"

namespace clauds::AutoSwitch {

QString modeKey(Mode m)
{
    switch (m) {
    case Mode::Ask:
        return QStringLiteral("ask");
    case Mode::Auto:
        return QStringLiteral("auto");
    case Mode::Off:
        break;
    }
    return QStringLiteral("off");
}

Mode modeFromKey(const QString &key)
{
    if (key == QLatin1String("ask"))
        return Mode::Ask;
    if (key == QLatin1String("auto"))
        return Mode::Auto;
    return Mode::Off;
}

std::optional<Usage::Limit> fullest(const Usage &usage, const QDateTime &now)
{
    std::optional<Usage::Limit> best;
    for (const auto &l : usage.limits)
        if (!best || l.currentPercent(now) > best->currentPercent(now))
            best = l;
    return best;
}

std::optional<Proposal> proposal(const QList<Account> &accounts, const QString &activeID,
                                 const QHash<QString, Usage> &usage, const QDateTime &now)
{
    auto isNew = [&now](const Usage &u) { return u.sampledAt.secsTo(now) < maxAge; };
    if (activeID.isEmpty() || !usage.contains(activeID))
        return std::nullopt;
    const Usage &mine = usage[activeID];
    if (!isNew(mine))
        return std::nullopt;
    const auto limit = fullest(mine, now);
    if (!limit || limit->currentPercent(now) < threshold)
        return std::nullopt;

    std::optional<Proposal> best;
    for (const auto &account : accounts) {
        if (account.id == activeID || !usage.contains(account.id))
            continue;
        const Usage &theirs = usage[account.id];
        if (!theirs.isLive || !isNew(theirs))
            continue;
        const auto top = fullest(theirs, now);
        if (!top)
            continue;
        const int p = top->currentPercent(now);
        if (p > threshold - margin)
            continue;
        if (!best || p < best->targetPercent)
            best = Proposal{account, *limit, p};
    }
    return best;
}

} // namespace clauds::AutoSwitch
