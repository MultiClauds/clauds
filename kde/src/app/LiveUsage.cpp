// SPDX-License-Identifier: MIT
#include "LiveUsage.h"
#include "Profiles.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QWebEnginePage>

#include <memory>

namespace clauds {

namespace {

const QUrl basePage(QStringLiteral("https://claude.ai/robots.txt"));

/// Same request as the macOS app. Each request stops after 15 seconds. The async function's result
/// is parked in a window variable, because runJavaScript() does not wait for a Promise.
const char *script = R"JS(
window.__claudsUsage = undefined;
(async () => {
  try {
    let org = null;
    const cookie = document.cookie.split('; ').find(c => c.startsWith('lastActiveOrg='));
    if (cookie) org = cookie.split('=')[1];
    if (!org) {
      const r = await fetch('/api/organizations', { credentials: 'include', signal: AbortSignal.timeout(15000) });
      if (!r.ok) return JSON.stringify({ status: r.status, step: 'organizations', body: (await r.text()).slice(0, 300) });
      const list = await r.json();
      org = (list.find(o => (o.capabilities || []).includes('chat')) || list[0] || {}).uuid;
    }
    if (!org) return JSON.stringify({ status: 0, step: 'no organization' });
    const r = await fetch('/api/organizations/' + org + '/usage', { credentials: 'include', signal: AbortSignal.timeout(15000) });
    if (!r.ok) return JSON.stringify({ status: r.status, step: 'usage', body: (await r.text()).slice(0, 300) });
    return JSON.stringify({ status: 200, usage: await r.json() });
  } catch (e) {
    return JSON.stringify({ status: -1, step: String(e) });
  }
})().then(result => { window.__claudsUsage = result; });
)JS";

} // namespace

LiveUsage::LiveUsage(QObject *parent)
    : QObject(parent)
{
}

QString LiveUsage::failureText(const QString &reason)
{
    if (reason.contains(QLatin1String("did not load")) || reason.contains(QLatin1String("timed out"), Qt::CaseInsensitive)
        || reason.contains(QLatin1String("answered -1")))
        return QStringLiteral("No connection to claude.ai");
    if (reason.contains(QLatin1String("format changed")) || reason.contains(QLatin1String("unexpected answer")))
        return QStringLiteral("claude.ai gave an answer that the app does not know");
    return QStringLiteral("Could not load new usage numbers");
}

QWebEnginePage *LiveUsage::pageFor(const QString &accountID)
{
    if (auto page = m_pages.value(accountID))
        return page;
    auto *page = new QWebEnginePage(Profiles::forAccount(accountID), this);
    m_pages.insert(accountID, page);
    return page;
}

void LiveUsage::forget(const QString &accountID)
{
    if (auto page = m_pages.take(accountID))
        page->deleteLater();
}

void LiveUsage::fetch(const QString &accountID, Callback done)
{
    QWebEnginePage *page = pageFor(accountID);
    if (page->url().host() == QLatin1String("claude.ai")) {
        runScript(page, done, 0);
        return;
    }
    // Load the base page first. Give up after 20 seconds.
    auto finished = std::make_shared<bool>(false);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(page, &QWebEnginePage::loadFinished, this, [=, this](bool ok) {
        if (*finished)
            return;
        *finished = true;
        disconnect(*conn);
        if (!ok)
            done(std::nullopt, State::Failed, QStringLiteral("claude.ai did not load"));
        else
            runScript(page, done, 0);
    });
    QTimer::singleShot(20000, this, [=] {
        if (*finished)
            return;
        *finished = true;
        disconnect(*conn);
        done(std::nullopt, State::Failed, QStringLiteral("claude.ai did not load (timed out)"));
    });
    page->load(basePage);
}

void LiveUsage::runScript(QWebEnginePage *page, Callback done, int attempt)
{
    if (attempt == 0)
        page->runJavaScript(QString::fromUtf8(script));
    // Poll for the result: every 250 ms, up to 35 s.
    QTimer::singleShot(250, this, [=, this] {
        page->runJavaScript(QStringLiteral("window.__claudsUsage"), [=, this](const QVariant &value) {
            const QString raw = value.toString();
            if (raw.isEmpty()) {
                if (attempt < 140)
                    runScript(page, done, attempt + 1);
                else
                    done(std::nullopt, State::Failed, QStringLiteral("the request timed out"));
                return;
            }
            const auto object = QJsonDocument::fromJson(raw.toUtf8()).object();
            if (!object.contains(QStringLiteral("status"))) {
                done(std::nullopt, State::Failed, QStringLiteral("unexpected answer"));
                return;
            }
            const int status = object.value(QStringLiteral("status")).toInt();
            if (status == 401 || status == 403) {
                done(std::nullopt, State::NeedsSignIn, {});
                return;
            }
            if (status != 200 || !object.value(QStringLiteral("usage")).isObject()) {
                done(std::nullopt, State::Failed,
                     QStringLiteral("claude.ai answered %1 (%2)").arg(status).arg(object.value(QStringLiteral("step")).toString()));
                return;
            }
            const auto parsed = Usage::parseLive(object.value(QStringLiteral("usage")).toObject());
            if (!parsed) {
                done(std::nullopt, State::Failed, QStringLiteral("usage format changed"));
                return;
            }
            done(parsed, State::Ok, {});
        });
    });
}

void LiveUsage::refresh(AccountStore *store, const QStringList &ids)
{
    if (ids.isEmpty())
        return;
    const QString id = ids.first();
    const QStringList rest = ids.mid(1);
    store->setLoading(id, true);
    QPointer<AccountStore> guard(store);
    fetch(id, [=, this](std::optional<Usage> usage, State state, QString detail) {
        if (!guard)
            return;
        guard->setLiveResult(id, usage, state, detail);
        refresh(guard, rest);
    });
}

} // namespace clauds
