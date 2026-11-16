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
        // A shared Electron that runs the app from the install folder.
        writeFile(tmp.path() + QStringLiteral("/usr/lib/electron/electron"), "");
        fakeProc(proc, 400, tmp.path() + QStringLiteral("/usr/lib/electron/electron"),
                 {QStringLiteral("electron"), install + QStringLiteral("/app.asar"), QStringLiteral("--type=gpu-process")});

        ClaudeProcess cp(exe, {install}, proc);
        QCOMPARE(cp.all().size(), 4);
        QCOMPARE(cp.mains().size(), 2);
        QCOMPARE(cp.mainsFor({}).size(), 1);
        QCOMPARE(cp.mainsFor({}).first().pid, 100);
        QCOMPARE(cp.mainsFor(QStringLiteral("/home/u/.local/share/clauds/Profiles/x")).first().pid, 200);
        QVERIFY(cp.anyRunning());
    }

    void separateUserDataDirArgument()
    {
        ClaudeProcess::Proc p{1, QStringLiteral("/usr/lib/claude-desktop/claude-desktop"),
                              {QStringLiteral("claude"), QStringLiteral("--user-data-dir"), QStringLiteral("/a/b/")}};
        QCOMPARE(p.userDataDir(), QStringLiteral("/a/b"));
        QVERIFY(ClaudeProcess({}, {}).isMain(p));
    }

    /// The process list of the official claude-desktop package on Kubuntu 26.04 (Electron 45),
    /// as seen with "ps -eo pid,args" on a real machine. Paths are shortened.
    void officialPackageProcessTree()
    {
        const QString root = tmp.path() + QStringLiteral("/real");
        const QString install = root + QStringLiteral("/usr/lib/claude-desktop");
        const QString exe = install + QStringLiteral("/claude-desktop");
        const QString data = root + QStringLiteral("/home/u/.config/Claude");
        const QString code = data + QStringLiteral("/claude-code/2.1.325/00b1b977bcc3/claude");
        for (const auto &f : {exe, install + QStringLiteral("/chrome_crashpad_handler"),
                              install + QStringLiteral("/resources/disclaimer"),
                              install + QStringLiteral("/resources/cowork-linux-helper"),
                              install + QStringLiteral("/resources/chrome-native-host"), code})
            writeFile(f, "");
        const QString proc = root + QStringLiteral("/proc");
        fakeProc(proc, 13370, exe, {exe});
        fakeProc(proc, 13644, exe, {exe, QStringLiteral("--type=zygote"), QStringLiteral("--no-zygote-sandbox")});
        fakeProc(proc, 14002, install + QStringLiteral("/chrome_crashpad_handler"),
                 {install + QStringLiteral("/chrome_crashpad_handler"), QStringLiteral("--database=") + data + QStringLiteral("/Crashpad")});
        fakeProc(proc, 14252, exe, {exe, QStringLiteral("--type=gpu-process"), QStringLiteral("--user-data-dir=") + data});
        fakeProc(proc, 15617, install + QStringLiteral("/resources/cowork-linux-helper"),
                 {install + QStringLiteral("/resources/cowork-linux-helper"), QStringLiteral("-socket"), QStringLiteral("/run/user/1000/x.sock")});
        fakeProc(proc, 40936, install + QStringLiteral("/resources/disclaimer"),
                 {install + QStringLiteral("/resources/disclaimer"), QStringLiteral("--pgroup"), QStringLiteral("--"), code});
        fakeProc(proc, 40937, code, {code, QStringLiteral("--output-format"), QStringLiteral("stream-json")});
        fakeProc(proc, 50000, install + QStringLiteral("/resources/chrome-native-host"),
                 {install + QStringLiteral("/resources/chrome-native-host"), QStringLiteral("chrome-extension://x/")});

        ClaudeProcess cp(exe, {install}, proc);
        // One open copy, on the normal data folder, and nothing else counts as a copy.
        QCOMPARE(cp.mains().size(), 1);
        QCOMPARE(cp.mains().first().pid, 13370);
        QCOMPARE(cp.mainsFor({}).size(), 1);
        QVERIFY(cp.mainsFor(data).isEmpty());
        // Helpers: zygote, crashpad, GPU, Cowork, disclaimer. Never the browser's native host.
        QCOMPARE(cp.all().size(), 6);
        for (const auto &p : cp.all())
            QVERIFY(p.pid != 50000);
        // A Code session keeps the data folder busy.
        QCOMPARE(cp.runningFrom({data}).size(), 1);
        QCOMPARE(cp.runningFrom({data}).first().pid, 40937);
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
