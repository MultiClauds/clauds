// SPDX-License-Identifier: MIT
#include "ClaudeProcess.h"
#include "TestUtil.h"

#include <QTemporaryDir>
#include <QTest>

using namespace clauds;

class ClaudeProcessTest : public QObject
{
    Q_OBJECT
    QTemporaryDir tmp;

    void fakeProc(const QString &proc, int pid, const QString &exe, const QStringList &args)
    {
        const QString dir = proc + QLatin1Char('/') + QString::number(pid);
        QDir().mkpath(dir);
        QFile::link(exe, dir + QStringLiteral("/exe"));
        writeFile(dir + QStringLiteral("/cmdline"), args.join(QChar(0)).toUtf8() + '\0');
    }

private Q_SLOTS:
    void findsCopiesByInstallFolder()
    {
        const QString install = tmp.path() + QStringLiteral("/usr/lib/claude-desktop");
        const QString exe = install + QStringLiteral("/claude-desktop");
        writeFile(exe, "");
        const QString proc = tmp.path() + QStringLiteral("/proc");
        fakeProc(proc, 100, exe, {exe});
        fakeProc(proc, 101, exe, {exe, QStringLiteral("--type=renderer")});
        fakeProc(proc, 200, exe, {exe, QStringLiteral("--user-data-dir=/home/u/.local/share/clauds/Profiles/x/")});
        fakeProc(proc, 300, QStringLiteral("/usr/bin/claude"), {QStringLiteral("claude")});   // Claude Code CLI: not ours
        writeFile(proc + QStringLiteral("/self/cmdline"), "x");

        ClaudeProcess cp(exe, {install}, proc);
        QCOMPARE(cp.all().size(), 3);
        QCOMPARE(cp.mains().size(), 2);
        QCOMPARE(cp.mainsFor({}).size(), 1);
        QCOMPARE(cp.mainsFor({}).first().pid, 100);
        QCOMPARE(cp.mainsFor(QStringLiteral("/home/u/.local/share/clauds/Profiles/x")).first().pid, 200);
        QVERIFY(cp.anyRunning());
    }

    void separateUserDataDirArgument()
    {
        ClaudeProcess::Proc p{1, {}, {QStringLiteral("claude"), QStringLiteral("--user-data-dir"), QStringLiteral("/a/b/")}};
        QCOMPARE(p.userDataDir(), QStringLiteral("/a/b"));
        QVERIFY(p.isMain());
    }

    void notInstalled()
    {
        ClaudeProcess cp({}, {}, tmp.path());
        QVERIFY(!cp.isInstalled());
        QVERIFY(cp.all().isEmpty());
        QVERIFY(!cp.launch());
        QVERIFY(cp.quitAll());
    }
};

QTEST_GUILESS_MAIN(ClaudeProcessTest)
#include "ClaudeProcessTest.moc"
