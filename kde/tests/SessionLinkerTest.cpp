// SPDX-License-Identifier: MIT
#include "SessionLinker.h"
#include "TestUtil.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>
#include <thread>

using namespace clauds;
namespace fs = std::filesystem;

class SessionLinkerTest : public QObject
{
    Q_OBJECT

    QTemporaryDir tmp;
    QString claudeA, claudeB, shared;

    QString session(const QString &dataDir, const QString &account, const QString &org, const QString &id)
    {
        return dataDir + QStringLiteral("/claude-code-sessions/%1/%2/local_%3.json").arg(account, org, id);
    }

    /// Every regular file under a folder (follows no symlinks), relative names.
    QStringList filesUnder(const QString &dir)
    {
        QStringList out;
        QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext())
            out << it.next();
        return out;
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(tmp.isValid());
        const QString root = tmp.path() + QStringLiteral("/") + QString::fromLatin1(QTest::currentTestFunction());
        claudeA = root + QStringLiteral("/A");
        claudeB = root + QStringLiteral("/B");
        shared = root + QStringLiteral("/shared");
        QDir().mkpath(shared);
    }

    void sessionIDs()
    {
        QCOMPARE(SessionLinker::sessionID(P(QStringLiteral("/x/local_abc.json"))), QStringLiteral("local_abc"));
        QVERIFY(SessionLinker::sessionID(P(QStringLiteral("/x/archived-sessions.idx"))).isEmpty());
        QVERIFY(SessionLinker::sessionID(P(QStringLiteral("/x/other.json"))).isEmpty());
    }

    void noSessionsFolderDoesNothing()
    {
        QDir().mkpath(claudeA);
        QCOMPARE(SessionLinker::link(P(claudeA), P(shared)), 0);
        QVERIFY(!QFileInfo::exists(shared + QStringLiteral("/orgs")));
    }

    void linkMovesFilesAndRecordsOwners()
    {
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s1")), "{\"x\":1}");
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s2")), "{\"isArchived\":true}");

        QCOMPARE(SessionLinker::link(P(claudeA), P(shared), QStringLiteral("switcher:A")), 1);

        const QString accountDir = claudeA + QStringLiteral("/claude-code-sessions/acctA");
        QVERIFY(QFileInfo(accountDir).isSymLink());
        QCOMPARE(QFileInfo(accountDir).symLinkTarget(), QFileInfo(shared + QStringLiteral("/orgs")).absoluteFilePath());
        QVERIFY(QFileInfo::exists(shared + QStringLiteral("/orgs/org1/local_s1.json")));
        // Claude sees the files through the symlink.
        QVERIFY(QFileInfo::exists(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s1"))));

        const auto owners = SessionLinker::loadOwners(P(shared));
        QCOMPARE(owners.value(QStringLiteral("local_s1")), QStringLiteral("acctA"));
        QCOMPARE(owners.value(SessionLinker::signedInKey(QStringLiteral("switcher:A"))), QStringLiteral("acctA"));

        // Archive list rebuilt from the isArchived marks.
        const auto idx = QJsonDocument::fromJson(readFile(shared + QStringLiteral("/orgs/org1/archived-sessions.idx"))).object();
        QCOMPARE(idx.value(QStringLiteral("archived")).toArray(), QJsonArray{QStringLiteral("local_s2")});

        // Private folders.
        QCOMPARE(QFileInfo(shared + QStringLiteral("/orgs")).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());

        // Running again changes nothing.
        QCOMPARE(SessionLinker::link(P(claudeA), P(shared), QStringLiteral("switcher:A")), 0);
    }

    void roundTripKeepsEachAccountsOwnSessions()
    {
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("a1")), "a1");
        writeFile(session(claudeB, QStringLiteral("acctB"), QStringLiteral("org1"), QStringLiteral("b1")), "b1");
        SessionLinker::link(P(claudeA), P(shared), QStringLiteral("switcher:A"));
        SessionLinker::link(P(claudeB), P(shared), QStringLiteral("switcher:B"));
        QCOMPARE(QDir(shared + QStringLiteral("/orgs/org1")).entryList({QStringLiteral("local_*")}).size(), 2);

        QCOMPARE(SessionLinker::unlink(P(claudeA), P(shared), QStringLiteral("switcher:A")), 1);
        const QString orgA = claudeA + QStringLiteral("/claude-code-sessions/acctA/org1");
        QVERIFY(!QFileInfo(claudeA + QStringLiteral("/claude-code-sessions/acctA")).isSymLink());
        QCOMPARE(QDir(orgA).entryList({QStringLiteral("local_*")}), QStringList{QStringLiteral("local_a1.json")});
        // B's session stays in the pool. One live file per session.
        QVERIFY(QFileInfo::exists(shared + QStringLiteral("/orgs/org1/local_b1.json")));
        QVERIFY(!QFileInfo::exists(shared + QStringLiteral("/orgs/org1/local_a1.json")));
    }

    void sessionMadeWhileSharedGoesToSignedInAccount()
    {
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("a1")), "a1");
        SessionLinker::link(P(claudeA), P(shared), QStringLiteral("switcher:A"));
        // A new session appears in the pool while shared; the outgoing account claims it.
        writeFile(shared + QStringLiteral("/orgs/org1/local_new.json"), "new");
        SessionLinker::claimUnknown(P(shared), QStringLiteral("switcher:A"));
        QCOMPARE(SessionLinker::loadOwners(P(shared)).value(QStringLiteral("local_new")), QStringLiteral("switcher:A"));

        SessionLinker::unlink(P(claudeA), P(shared), QStringLiteral("switcher:A"));
        QCOMPARE(SessionLinker::loadOwners(P(shared)).value(QStringLiteral("local_new")), QStringLiteral("acctA"));
        QVERIFY(QFileInfo::exists(claudeA + QStringLiteral("/claude-code-sessions/acctA/org1/local_new.json")));
    }

    void conflictKeepsNewerAndParksOlder()
    {
        writeFile(shared + QStringLiteral("/orgs/org1/local_s1.json"), "old");
        fs::last_write_time(P(shared + QStringLiteral("/orgs/org1/local_s1.json")), fs::file_time_type::clock::now() - std::chrono::hours(1));
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s1")), "new");

        SessionLinker::link(P(claudeA), P(shared));
        QCOMPARE(readFile(shared + QStringLiteral("/orgs/org1/local_s1.json")), QByteArray("new"));
        const auto parked = filesUnder(shared + QStringLiteral("/_conflicts"));
        QCOMPARE(parked.size(), 1);
        QCOMPARE(readFile(parked.first()), QByteArray("old"));
    }

    void exactCopyIsNotKeptTwice()
    {
        writeFile(shared + QStringLiteral("/orgs/org1/local_s1.json"), "same");
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s1")), "same");
        SessionLinker::link(P(claudeA), P(shared));
        QVERIFY(!QFileInfo::exists(shared + QStringLiteral("/_conflicts")));
    }

    void unreadableOwnersStopsEverything()
    {
        writeFile(SessionLinker::ownersFile(P(shared)).c_str(), "{ not json");
        const QString file = session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s1"));
        writeFile(file, "x");
        QVERIFY_THROWS_EXCEPTION(SessionLinker::Error, SessionLinker::link(P(claudeA), P(shared)));
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(!QFileInfo(claudeA + QStringLiteral("/claude-code-sessions/acctA")).isSymLink());
        QCOMPARE(readFile(SessionLinker::ownersFile(P(shared)).c_str()), QByteArray("{ not json"));
    }

    void foreignSymlinkIsNotTouched()
    {
        const QString elsewhere = tmp.path() + QStringLiteral("/elsewhere");
        QDir().mkpath(elsewhere);
        QDir().mkpath(claudeA + QStringLiteral("/claude-code-sessions"));
        const QString accountDir = claudeA + QStringLiteral("/claude-code-sessions/acctX");
        QVERIFY(QFile::link(elsewhere, accountDir));
        SessionLinker::unlink(P(claudeA), P(shared));
        QVERIFY(QFileInfo(accountDir).isSymLink());
        QCOMPARE(QFileInfo(accountDir).symLinkTarget(), elsewhere);
    }

    void ownersBackupIsKept()
    {
        writeFile(session(claudeA, QStringLiteral("acctA"), QStringLiteral("org1"), QStringLiteral("s1")), "1");
        SessionLinker::link(P(claudeA), P(shared));
        writeFile(session(claudeB, QStringLiteral("acctB"), QStringLiteral("org1"), QStringLiteral("s2")), "2");
        SessionLinker::link(P(claudeB), P(shared));
        QVERIFY(QFileInfo::exists(shared + QStringLiteral("/owners.json.bak")));
        QCOMPARE(QFileInfo(SessionLinker::ownersFile(P(shared)).c_str()).permissions() & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
    }
};

QTEST_GUILESS_MAIN(SessionLinkerTest)
#include "SessionLinkerTest.moc"
