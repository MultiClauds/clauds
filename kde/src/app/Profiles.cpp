// SPDX-License-Identifier: MIT
#include "Profiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>

namespace clauds {

QHash<QString, QWebEngineProfile *> &Profiles::all()
{
    static QHash<QString, QWebEngineProfile *> map;
    return map;
}

QWebEngineProfile *Profiles::forAccount(const QString &id)
{
    if (auto *p = all().value(id))
        return p;
    auto *profile = new QWebEngineProfile(QStringLiteral("account-") + id, qApp);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    // Some sign-in pages (for example Google) refuse browsers that name an embedded engine.
    QString agent = profile->httpUserAgent();
    agent.remove(QRegularExpression(QStringLiteral("QtWebEngine/[^ ]+ ")));
    profile->setHttpUserAgent(agent);
    all().insert(id, profile);
    return profile;
}

void Profiles::forget(const QString &id)
{
    // The sign-in goes away with the cookies. The profile folder itself stays until the next start,
    // because the web engine may still hold its files open (see main.cpp: removeStaleProfiles).
    auto *p = forAccount(id);
    p->cookieStore()->deleteAllCookies();
    p->clearHttpCache();
}

} // namespace clauds
