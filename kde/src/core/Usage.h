// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QTimeZone>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace clauds {

/// Plan usage of one account.
struct Usage {
    /// One limit of the plan.
    struct Limit {
        QString label;      ///< "5h", "Week", "Fable wk"
        QString fullName;   ///< "5-hour limit", "Weekly limit, all models", ...
        int percent = 0;
        QDateTime resetsAt; ///< invalid when not known

        /// After the reset time, the window is empty.
        int currentPercent(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
        /// "1h 05m left", "Thu 13:59", or " " when not known.
        QString resetText(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
        QString tip(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
        bool operator==(const Limit &) const = default;
    };

    QList<Limit> limits;
    QDateTime sampledAt;
    bool isLive = false;

    /// "now", "12m ago", "3h ago", "2d ago".
    QString age(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
    bool operator==(const Usage &) const = default;

    /// Newest sample of <Claude data>/plan-usage-history.json (percent only, no reset times).
    static std::optional<Usage> readLocal(const QString &dataDir,
                                          const QDateTime &now = QDateTime::currentDateTimeUtc());
    /// Parses the answer of claude.ai /api/organizations/<org>/usage.
    static std::optional<Usage> parseLive(const QJsonObject &usage,
                                          const QDateTime &now = QDateTime::currentDateTimeUtc());
    static QDateTime parseDate(const QString &text);
};

} // namespace clauds
