// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QString>

class QWebEngineProfile;

namespace clauds {

/// One persistent web profile (cookies, local storage) per account. The hidden usage page and the
/// account's chat window share it, so one sign-in serves both. Linux counterpart of
/// WKWebsiteDataStore(forIdentifier:).
class Profiles
{
public:
    static QWebEngineProfile *forAccount(const QString &id);
    /// Deletes the account's web data (cookies, cache) after the account is removed.
    static void forget(const QString &id);

private:
    static QHash<QString, QWebEngineProfile *> &all();
};

} // namespace clauds
