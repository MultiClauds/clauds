// SPDX-License-Identifier: MIT
#include "ChatWindows.h"
#include "LiveUsage.h"
#include "Profiles.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineView>

namespace clauds {

namespace {

/// A page that opens pop-ups (for example "Sign in with Google") in their own window, in the same
/// web profile, so the sign-in lands in the account's profile.
class ChatPage : public QWebEnginePage
{
public:
    using QWebEnginePage::QWebEnginePage;

protected:
    QWebEnginePage *createWindow(WebWindowType) override
    {
        auto *view = new QWebEngineView;
        view->setAttribute(Qt::WA_DeleteOnClose);
        auto *page = new ChatPage(profile(), view);
        view->setPage(page);
        connect(page, &QWebEnginePage::windowCloseRequested, view, &QWidget::close);
        view->resize(520, 680);
        view->show();
        return page;
    }
};

/// Remembers its size and place per account.
class ChatWindow : public QMainWindow
{
public:
    explicit ChatWindow(const QString &key)
        : m_key(key)
    {
        QSettings s;
        if (!restoreGeometry(s.value(m_key).toByteArray()))
            resize(1100, 780);
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        QSettings().setValue(m_key, saveGeometry());
        QMainWindow::closeEvent(e);
    }

private:
    QString m_key;
};

} // namespace

ChatWindows::ChatWindows(LiveUsage *live, QObject *parent)
    : QObject(parent)
    , m_live(live)
{
}

ChatWindows::~ChatWindows()
{
    for (const auto &w : std::as_const(m_windows))
        if (w)
            w->deleteLater();
}

void ChatWindows::open(const QString &accountID, const QString &title, const QString &path)
{
    const QUrl url(QStringLiteral("https://claude.ai") + path);
    if (auto window = m_windows.value(accountID)) {
        window->setWindowTitle(title + QStringLiteral(" — Claude"));
        if (path != QLatin1String("/recents"))
            static_cast<QWebEngineView *>(window->centralWidget())->load(url);
        window->show();
        window->raise();
        window->activateWindow();
        return;
    }
    auto *window = new ChatWindow(QStringLiteral("ChatWindow/") + accountID);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(title + QStringLiteral(" — Claude"));
    window->setWindowIcon(QIcon::fromTheme(QStringLiteral("clauds"), QIcon(QStringLiteral(":/icons/clauds.svg"))));
    auto *view = new QWebEngineView(window);
    view->setPage(new ChatPage(Profiles::forAccount(accountID), view));
    view->load(url);
    window->setCentralWidget(view);
    m_windows.insert(accountID, window);
    window->show();
    window->raise();
    window->activateWindow();
}

void ChatWindows::openForSignIn(const QString &accountID, const QString &title, std::function<void()> onSignedIn)
{
    open(accountID, title, QStringLiteral("/login"));
    if (auto old = m_watchers.take(accountID))
        old->deleteLater();
    auto *timer = new QTimer(this);
    timer->setInterval(4000);
    m_watchers.insert(accountID, timer);
    auto ticks = std::make_shared<int>(0);
    auto busy = std::make_shared<bool>(false);
    connect(timer, &QTimer::timeout, this, [=, this] {
        auto window = m_windows.value(accountID);
        if (!window || !window->isVisible() || ++*ticks > 225) {
            timer->deleteLater();
            return;
        }
        if (*busy)
            return;
        *busy = true;
        QPointer<QTimer> guard(timer);
        m_live->fetch(accountID, [=, this](std::optional<Usage>, LiveUsage::State state, QString) {
            *busy = false;
            if (!guard || state != LiveUsage::State::Ok)
                return;
            guard->deleteLater();
            if (auto w = m_windows.take(accountID))
                w->close();   // the next open starts with a fresh page, not the sign-in page
            onSignedIn();
        });
    });
    timer->start();
}

void ChatWindows::close(const QString &accountID)
{
    if (auto t = m_watchers.take(accountID))
        t->deleteLater();
    if (auto w = m_windows.take(accountID))
        w->close();
}

} // namespace clauds
