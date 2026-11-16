// SPDX-License-Identifier: MIT
// Restart-mode switch on a temporary tree, with no Claude installed (nothing to quit or start).
#include "AccountStore.h"
#include "TestUtil.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace clauds;

class AccountStoreTest : public QObject
{
    Q_OBJECT
    QTemporaryDir tmp;

    AccountStore::Paths paths(const QString &name)
    {
        return {tmp.path() + QLatin1Char('/') + name + QStringLiteral("/config/Claude"),
                tmp.path() + QLatin1Char('/') + name + QStringLiteral("/share/clauds")};
    }

    void waitIdle(AccountStore &s)
    {
        QTRY_VERIFY_WITH_TIMEOUT(!s.isSwitching(), 10000);
        QTest::qWait(50);
        QTRY_VERIFY_WITH_TIMEOUT(!s.isSwitching(), 10000);
    }

private Q_SLOTS:
    void initTestCase() { qputenv("XDG_DATA_HOME", (tmp.path() + QStringLiteral("/xdg")).toUtf8()); }

    void switchRenamesFoldersBothWays()
    {
        const auto p = paths(QStringLiteral("switch"));
        writeFile(p.claudeDir + QStringLiteral("/login-of-A"), "A");
        AccountStore store(p, ClaudeProcess({}, {}));
        const QString a = store.adoptCurrentLogin(QStringLiteral("Work"));
        const QString b = store.addAccount(QStringLiteral("Personal"));
        QCOMPARE(store.activeID(), a);

        QSignalSpy errors(&store, &AccountStore::error);
        store.switchTo(b);
        waitIdle(store);
        // openClaude() reports a missing Claude; the switch itself must have worked.
        QCOMPARE(store.activeID(), b);
        QVERIFY(QFileInfo::exists(store.profileDir(a) + QStringLiteral("/login-of-A")));
        QVERIFY(!QFileInfo::exists(p.claudeDir));   // new account: Claude starts at sign-in

        writeFile(p.claudeDir + QStringLiteral("/login-of-B"), "B");
        store.switchTo(a);
        waitIdle(store);
        QCOMPARE(store.activeID(), a);
        QVERIFY(QFileInfo::exists(p.claudeDir + QStringLiteral("/login-of-A")));
        QVERIFY(QFileInfo::exists(store.profileDir(b) + QStringLiteral("/login-of-B")));

        // The state survives a restart.
        AccountStore again(p, ClaudeProcess({}, {}));
        QCOMPARE(again.activeID(), a);
        QCOMPARE(again.accounts().size(), 2);
    }

    void folderInTheWayChangesNothing()
    {
        const auto p = paths(QStringLiteral("inway"));
        writeFile(p.claudeDir + QStringLiteral("/login"), "A");
        AccountStore store(p, ClaudeProcess({}, {}));
        const QString a = store.adoptCurrentLogin(QStringLiteral("A"));
        const QString b = store.addAccount(QStringLiteral("B"));
        writeFile(store.profileDir(a) + QStringLiteral("/stale"), "x");
        QSignalSpy errors(&store, &AccountStore::error);
        store.switchTo(b);
        waitIdle(store);
        QCOMPARE(store.activeID(), a);
        QVERIFY(QFileInfo::exists(p.claudeDir + QStringLiteral("/login")));
        QVERIFY(errors.count() >= 1);
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("already")));
    }

    void removeGoesToTrashAndNotActive()
    {
        const auto p = paths(QStringLiteral("remove"));
        AccountStore store(p, ClaudeProcess({}, {}));
        const QString a = store.adoptCurrentLogin(QStringLiteral("A"));
        const QString b = store.addAccount(QStringLiteral("B"));
        writeFile(store.profileDir(b) + QStringLiteral("/data"), "x");
        QVERIFY(!store.remove(a));   // never the active account
        // Some build containers have no usable Trash. Then nothing may be lost: the data and
        // the account must both stay.
        if (store.remove(b)) {
            QVERIFY(!QFileInfo::exists(store.profileDir(b)));
            QCOMPARE(store.accounts().size(), 1);
        } else {
            QVERIFY(QFileInfo::exists(store.profileDir(b) + QStringLiteral("/data")));
            QCOMPARE(store.accounts().size(), 2);
            qWarning("Trash not available here: checked the failure path only");
        }
    }

    void sideBySideKeepsSessionsSeparate()
    {
        const auto p = paths(QStringLiteral("sbs"));
        writeFile(p.claudeDir + QStringLiteral("/claude-code-sessions/acctA/org1/local_a1.json"), "a1");
        AccountStore store(p, ClaudeProcess({}, {}));
        store.adoptCurrentLogin(QStringLiteral("A"));
        store.tidyIdleFolders();   // background: shared layout for the idle account
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(p.claudeDir + QStringLiteral("/claude-code-sessions/acctA")).isSymLink(), 5000);
        store.setSideBySide(true);
        QTRY_VERIFY_WITH_TIMEOUT(store.sideBySide(), 5000);
        waitIdle(store);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo(p.claudeDir + QStringLiteral("/claude-code-sessions/acctA")).isSymLink(), 5000);
        QVERIFY(QFileInfo::exists(p.claudeDir + QStringLiteral("/claude-code-sessions/acctA/org1/local_a1.json")));
        QVERIFY(!store.sessionsAreShared());
    }
};

QTEST_GUILESS_MAIN(AccountStoreTest)
#include "AccountStoreTest.moc"
