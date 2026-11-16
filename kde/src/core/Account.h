// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QString>

namespace clauds {

struct Account {
    QString id;     ///< UUID without braces
    QString name;
    QString email;  ///< optional, only shown in the panel

    bool operator==(const Account &) const = default;

    QJsonObject toJson() const
    {
        QJsonObject o{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}};
        if (!email.isEmpty())
            o.insert(QStringLiteral("email"), email);
        return o;
    }
    static Account fromJson(const QJsonObject &o)
    {
        return {o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("name")).toString(),
                o.value(QStringLiteral("email")).toString()};
    }
    /// Owner mark for sessions that an account makes while sessions are shared.
    static QString tag(const QString &id) { return QStringLiteral("switcher:") + id; }
};

} // namespace clauds
