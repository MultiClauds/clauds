// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>

#include <functional>

class QMainWindow;
class QTimer;

namespace clauds {

class LiveUsage;

/// One claude.ai window per account. Each window uses the account's own web profile, so every
/// account stays signed in at the same time, next to the desktop app.
class ChatWindows : public QObject
{
    Q_OBJECT
public:
    ChatWindows(LiveUsage *live, QObject *parent = nullptr);
    ~ChatWindows() override;

    void open(const QString &accountID, const QString &title, const QString &path = QStringLiteral("/recents"));
    /// Opens the sign-in page. Every 4 s it checks if the login is good; then it closes the window
    /// and calls `onSignedIn`. Stops when the window closes or after 15 minutes.
    void openForSignIn(const QString &accountID, const QString &title, std::function<void()> onSignedIn);
    void close(const QString &accountID);

private:
    LiveUsage *m_live;
    QHash<QString, QPointer<QMainWindow>> m_windows;
    QHash<QString, QPointer<QTimer>> m_watchers;
};

} // namespace clauds
