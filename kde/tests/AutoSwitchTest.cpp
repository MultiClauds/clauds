// SPDX-License-Identifier: MIT
// Same cases as AutoSwitch.selfCheck() in the macOS app.
#include "AutoSwitch.h"

#include <QTest>

using namespace clauds;

class AutoSwitchTest : public QObject
{
    Q_OBJECT
    Account a{QStringLiteral("a"), QStringLiteral("A"), {}};
    Account b{QStringLiteral("b"), QStringLiteral("B"), {}};
    Account c{QStringLiteral("c"), QStringLiteral("C"), {}};
    QDateTime now = QDateTime::currentDateTimeUtc();

    Usage use(int five, int week, bool live = true, qint64 age = 60)
    {
        return Usage{{{QStringLiteral("5h"), QStringLiteral("5-hour limit"), five, now.addSecs(3600)},
                      {QStringLiteral("Week"), QStringLiteral("Weekly limit, all models"), week, now.addSecs(86400)}},
                     now.addSecs(-age), live};
    }
    std::optional<AutoSwitch::Proposal> run(const QHash<QString, Usage> &u)
    {
        return AutoSwitch::proposal({a, b, c}, a.id, u, now);
    }

private Q_SLOTS:
    void cases()
    {
        QVERIFY2(!run({{a.id, use(89, 10)}, {b.id, use(5, 5)}}), "below the threshold: no switch");
        QCOMPARE(run({{a.id, use(92, 10)}, {b.id, use(40, 30)}, {c.id, use(10, 20)}})->target.id, c.id);
        QCOMPARE(run({{a.id, use(10, 95)}, {b.id, use(5, 5)}})->target.id, b.id);
        QVERIFY2(!run({{a.id, use(95, 10)}, {b.id, use(5, 85)}}), "the weekly limit of the target counts");
        QVERIFY2(!run({{a.id, use(95, 10)}, {b.id, use(81, 10)}}), "target too full");
        QCOMPARE(run({{a.id, use(95, 10)}, {b.id, use(80, 10)}})->target.id, b.id);
        QVERIFY2(!run({{a.id, use(95, 10)}, {b.id, use(5, 5, true, 3600)}}), "old numbers of the target");
        QVERIFY2(!run({{a.id, use(95, 10)}, {b.id, use(5, 5, false)}}), "target numbers not live");
        QVERIFY2(!run({{a.id, use(95, 10, true, 3600)}, {b.id, use(5, 5)}}), "old numbers of the current account");
        QVERIFY2(!run({{a.id, use(95, 10)}}), "no numbers for the target");
        auto past = use(99, 10);
        past.limits[0].resetsAt = now.addSecs(-60);
        QVERIFY2(!run({{a.id, past}, {b.id, use(5, 5)}}), "limit after its reset");
    }

    void modeKeysMatchMacOS()
    {
        QCOMPARE(AutoSwitch::modeKey(AutoSwitch::Mode::Ask), QStringLiteral("ask"));
        QCOMPARE(AutoSwitch::modeFromKey(QStringLiteral("auto")), AutoSwitch::Mode::Auto);
        QCOMPARE(AutoSwitch::modeFromKey(QStringLiteral("bogus")), AutoSwitch::Mode::Off);
    }
};

QTEST_GUILESS_MAIN(AutoSwitchTest)
#include "AutoSwitchTest.moc"
