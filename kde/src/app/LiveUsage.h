// SPDX-License-Identifier: MIT
#pragma once

#include "AccountStore.h"
#include "Usage.h"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>

class QWebEnginePage;

namespace clauds {

/// Reads the live plan usage (percent and reset time) of an account from claude.ai.
///
/// One hidden web page per account, in the account's own web profile (see Profiles). It loads one
/// tiny claude.ai page, then asks claude.ai for the usage numbers, as the website itself does.
///   - The only host it talks to is claude.ai.
///   - It does not read or store passwords or tokens. The web profile keeps the login.
///   - This is not an official interface. If it fails, the panel falls back to the local numbers.
class LiveUsage : public QObject
{
    Q_OBJECT
public:
    using State = AccountStore::LiveState;
    using Callback = std::function<void(std::optional<Usage>, State, QString)>;

    explicit LiveUsage(QObject *parent = nullptr);

    void fetch(const QString &accountID, Callback done);
    /// Reads every listed account, one after the other, and gives the results to the store.
    void refresh(AccountStore *store, const QStringList &ids);
    void forget(const QString &accountID);

    /// Plain words for the panel when a read failed.
    static QString failureText(const QString &reason);

private:
    void runScript(QWebEnginePage *page, Callback done, int attempt);
    QWebEnginePage *pageFor(const QString &accountID);
    QHash<QString, QPointer<QWebEnginePage>> m_pages;
    QString m_debugFile;
};

} // namespace clauds
