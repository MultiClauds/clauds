// SPDX-License-Identifier: MIT
#include "Usage.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace clauds {

int Usage::Limit::currentPercent(const QDateTime &now) const
{
    if (resetsAt.isValid() && now >= resetsAt)
        return 0;
    return percent;
}

QString Usage::Limit::resetText(const QDateTime &now) const
{
    if (!resetsAt.isValid())
        return QStringLiteral(" ");
    const qint64 minutes = now.secsTo(resetsAt) / 60;
    if (minutes <= 0)
        return QStringLiteral("reset");
    if (minutes < 60)
        return QStringLiteral("%1m left").arg(minutes);
    if (minutes < 24 * 60)
        return QStringLiteral("%1h %2m left").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
    return QLocale().toString(resetsAt.toLocalTime(), QStringLiteral("ddd HH:mm"));
}

QString Usage::Limit::tip(const QDateTime &now) const
{
    QString text = QStringLiteral("%1: %2% used").arg(fullName).arg(currentPercent(now));
    if (resetsAt.isValid())
        text += QStringLiteral("\nResets: ") + QLocale().toString(resetsAt.toLocalTime(), QLocale::ShortFormat);
    return text;
}

QString Usage::age(const QDateTime &now) const
{
    const qint64 seconds = std::max<qint64>(0, sampledAt.secsTo(now));
    if (seconds < 90)
        return QStringLiteral("now");
    if (seconds < 3600)
        return QStringLiteral("%1m ago").arg(seconds / 60);
    if (seconds < 48 * 3600)
        return QStringLiteral("%1h ago").arg(seconds / 3600);
    return QStringLiteral("%1d ago").arg(seconds / 86400);
}

std::optional<Usage> Usage::readLocal(const QString &dataDir, const QDateTime &now)
{
    QFile f(dataDir + QStringLiteral("/plan-usage-history.json"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto samples = doc.object().value(QStringLiteral("samples")).toArray();
    if (samples.isEmpty())
        return std::nullopt;
    QJsonObject last;
    double lastT = -1;
    for (const auto &s : samples) {
        const auto o = s.toObject();
        const double t = o.value(QStringLiteral("t")).toDouble(-1);
        if (t > lastT) {
            lastT = t;
            last = o;
        }
    }
    if (lastT < 0)
        return std::nullopt;
    Usage usage;
    usage.sampledAt = QDateTime::fromMSecsSinceEpoch(qint64(lastT), QTimeZone::utc());
    const auto u = last.value(QStringLiteral("u")).toObject();
    // The 5-hour number is only true while the sample is younger than 5 hours.
    if (usage.sampledAt.secsTo(now) < 5 * 3600 && u.contains(QStringLiteral("fh")))
        usage.limits.append({QStringLiteral("5h"), QStringLiteral("5-hour limit"),
                             int(std::lround(u.value(QStringLiteral("fh")).toDouble())), {}});
    if (u.contains(QStringLiteral("sd")))
        usage.limits.append({QStringLiteral("Week"), QStringLiteral("Weekly limit, all models"),
                             int(std::lround(u.value(QStringLiteral("sd")).toDouble())), {}});
    usage.isLive = false;
    return usage;
}

QDateTime Usage::parseDate(const QString &text)
{
    QDateTime d = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (d.isValid())
        return d.toUTC();
    QString plain = text;
    plain.remove(QRegularExpression(QStringLiteral("\\.\\d+")));
    d = QDateTime::fromString(plain, Qt::ISODate);
    return d.isValid() ? d.toUTC() : QDateTime();
}

std::optional<Usage> Usage::parseLive(const QJsonObject &usage, const QDateTime &now)
{
    QList<Limit> limits;
    auto has = [&limits](const QString &label) {
        return std::any_of(limits.begin(), limits.end(), [&](const Limit &l) { return l.label == label; });
    };
    for (const auto &value : usage.value(QStringLiteral("limits")).toArray()) {
        const auto entry = value.toObject();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        if (kind.isEmpty() || !entry.value(QStringLiteral("percent")).isDouble())
            continue;
        const double percent = entry.value(QStringLiteral("percent")).toDouble();
        const QDateTime resetsAt = parseDate(entry.value(QStringLiteral("resets_at")).toString());
        const QString model = entry.value(QStringLiteral("scope")).toObject().value(QStringLiteral("model")).toObject()
                                  .value(QStringLiteral("display_name")).toString();
        QString label, fullName;
        if (kind == QLatin1String("session")) {
            label = QStringLiteral("5h");
            fullName = QStringLiteral("5-hour limit");
        } else if (kind == QLatin1String("weekly_all")) {
            label = QStringLiteral("Week");
            fullName = QStringLiteral("Weekly limit, all models");
        } else if (kind == QLatin1String("weekly_scoped")) {
            if (model.isEmpty())
                continue;
            label = model + QStringLiteral(" wk");
            fullName = QStringLiteral("Weekly limit, %1 only").arg(model);
        } else {
            continue;
        }
        if (has(label))
            continue;
        limits.append({label, fullName, int(std::lround(percent)), resetsAt});
    }
    if (limits.isEmpty()) {
        auto window = [&](const char *key, const QString &label, const QString &fullName) {
            const auto entry = usage.value(QLatin1String(key)).toObject();
            if (entry.isEmpty())
                return;
            limits.append({label, fullName, int(std::lround(entry.value(QStringLiteral("utilization")).toDouble(0))),
                           parseDate(entry.value(QStringLiteral("resets_at")).toString())});
        };
        window("five_hour", QStringLiteral("5h"), QStringLiteral("5-hour limit"));
        window("seven_day", QStringLiteral("Week"), QStringLiteral("Weekly limit, all models"));
    }
    if (limits.isEmpty())
        return std::nullopt;
    auto order = [](const Limit &l) {
        if (l.label == QLatin1String("5h"))
            return 0;
        if (l.label == QLatin1String("Week"))
            return 1;
        return 2;
    };
    std::stable_sort(limits.begin(), limits.end(), [&](const Limit &a, const Limit &b) { return order(a) < order(b); });
    Usage result;
    result.limits = limits.mid(0, 3);
    result.sampledAt = now;
    result.isLive = true;
    return result;
}

} // namespace clauds
