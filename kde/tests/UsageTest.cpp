// SPDX-License-Identifier: MIT
#include "Usage.h"
#include "TestUtil.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace clauds;

class UsageTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parseLimits()
    {
        const auto now = QDateTime(QDate(2026, 11, 16), QTime(10, 0), QTimeZone::utc());
        const auto json = QJsonDocument::fromJson(R"({"limits":[
            {"kind":"weekly_scoped","percent":40,"resets_at":"2026-11-20T13:59:00.634895+00:00","scope":{"model":{"display_name":"Fable"}}},
            {"kind":"weekly_all","percent":22.6,"resets_at":"2026-11-20T13:59:00+00:00"},
            {"kind":"session","percent":18,"resets_at":"2026-11-16T11:05:00Z"},
            {"kind":"unknown","percent":99}
        ]})").object();
        const auto u = Usage::parseLive(json, now);
        QVERIFY(u);
        QVERIFY(u->isLive);
        QCOMPARE(u->limits.size(), 3);
        QCOMPARE(u->limits[0].label, QStringLiteral("5h"));
        QCOMPARE(u->limits[1].label, QStringLiteral("Week"));
        QCOMPARE(u->limits[1].percent, 23);
        QCOMPARE(u->limits[2].label, QStringLiteral("Fable wk"));
        QVERIFY(u->limits[2].resetsAt.isValid());
        QCOMPARE(u->limits[0].resetText(now), QStringLiteral("1h 05m left"));
    }

    void parseOldFormat()
    {
        const auto json = QJsonDocument::fromJson(R"({"five_hour":{"utilization":50,"resets_at":null},
                                                      "seven_day":{"utilization":10}})").object();
        const auto u = Usage::parseLive(json);
        QVERIFY(u);
        QCOMPARE(u->limits.size(), 2);
        QCOMPARE(u->limits[0].percent, 50);
    }

    void noLimitsAnswer()
    {
        const auto json = QJsonDocument::fromJson(R"({"five_hour":null,"seven_day":null,"limits":[],"spend":{"x":1}})").object();
        QVERIFY(!Usage::parseLive(json));
        QVERIFY(Usage::hasNoLimits(json));
        QCOMPARE(Usage::shape(json), QStringLiteral("five_hour: null, seven_day: null, limits: array[0]"));
        const auto some = QJsonDocument::fromJson(R"({"five_hour":{"utilization":5},"limits":[]})").object();
        QVERIFY(!Usage::hasNoLimits(some));
        QVERIFY(!Usage::hasNoLimits(QJsonObject{}));
    }

    void parseNothing() { QVERIFY(!Usage::parseLive(QJsonObject{})); }

    void afterResetIsEmpty()
    {
        const auto now = QDateTime::currentDateTimeUtc();
        Usage::Limit l{QStringLiteral("5h"), QStringLiteral("5-hour limit"), 99, now.addSecs(-60)};
        QCOMPARE(l.currentPercent(now), 0);
        QCOMPARE(l.resetText(now), QStringLiteral("reset"));
    }

    void readLocalFile()
    {
        QTemporaryDir dir;
        const auto now = QDateTime::currentDateTimeUtc();
        const qint64 recent = now.addSecs(-600).toMSecsSinceEpoch();
        writeFile(dir.path() + QStringLiteral("/plan-usage-history.json"),
                  QStringLiteral(R"({"samples":[{"t":1000,"u":{"fh":90,"sd":90}},{"t":%1,"u":{"fh":12.4,"sd":33.5}}]})")
                      .arg(recent).toUtf8());
        const auto u = Usage::readLocal(dir.path(), now);
        QVERIFY(u);
        QVERIFY(!u->isLive);
        QCOMPARE(u->limits.size(), 2);
        QCOMPARE(u->limits[0].percent, 12);
        QCOMPARE(u->limits[1].percent, 34);
        QCOMPARE(u->age(now), QStringLiteral("10m ago"));
        // An old sample has no 5-hour number.
        const auto later = Usage::readLocal(dir.path(), now.addSecs(6 * 3600));
        QCOMPARE(later->limits.size(), 1);
    }
};

QTEST_GUILESS_MAIN(UsageTest)
#include "UsageTest.moc"
