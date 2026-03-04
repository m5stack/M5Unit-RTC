/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  UnitTest for PCF8563 types (native)
*/
#include <gtest/gtest.h>
#include <unit/unit_PCF8563_types.hpp>

using namespace m5::unit::pcf8563;

// ============================================================
// rtc_time_t
// ============================================================

class RtcTimeTest : public ::testing::Test {};

TEST_F(RtcTimeTest, DefaultConstructor)
{
    rtc_time_t t;
    EXPECT_EQ(t.hours, -1);
    EXPECT_EQ(t.minutes, -1);
    EXPECT_EQ(t.seconds, -1);
}

TEST_F(RtcTimeTest, ParameterizedConstructor)
{
    rtc_time_t t(12, 30, 45);
    EXPECT_EQ(t.hours, 12);
    EXPECT_EQ(t.minutes, 30);
    EXPECT_EQ(t.seconds, 45);
}

TEST_F(RtcTimeTest, ConstructFromTm)
{
    struct tm src{};
    src.tm_hour = 23;
    src.tm_min  = 59;
    src.tm_sec  = 58;

    rtc_time_t t(src);
    EXPECT_EQ(t.hours, 23);
    EXPECT_EQ(t.minutes, 59);
    EXPECT_EQ(t.seconds, 58);
}

TEST_F(RtcTimeTest, FromTm)
{
    struct tm src{};
    src.tm_hour = 8;
    src.tm_min  = 15;
    src.tm_sec  = 0;

    auto t = rtc_time_t::from_tm(src);
    EXPECT_EQ(t.hours, 8);
    EXPECT_EQ(t.minutes, 15);
    EXPECT_EQ(t.seconds, 0);
}

TEST_F(RtcTimeTest, ToTm)
{
    rtc_time_t t(14, 20, 33);
    struct tm result = t.to_tm();
    EXPECT_EQ(result.tm_hour, 14);
    EXPECT_EQ(result.tm_min, 20);
    EXPECT_EQ(result.tm_sec, 33);
}

TEST_F(RtcTimeTest, RoundTripTm)
{
    rtc_time_t original(10, 25, 50);
    struct tm mid   = original.to_tm();
    rtc_time_t back = rtc_time_t::from_tm(mid);
    EXPECT_EQ(original, back);
}

TEST_F(RtcTimeTest, EqualityOperator)
{
    rtc_time_t a(12, 30, 45);
    rtc_time_t b(12, 30, 45);
    EXPECT_EQ(a, b);

    // Differ by hours
    rtc_time_t c(13, 30, 45);
    EXPECT_NE(a, c);

    // Differ by minutes
    rtc_time_t d(12, 31, 45);
    EXPECT_NE(a, d);

    // Differ by seconds
    rtc_time_t e(12, 30, 46);
    EXPECT_NE(a, e);
}

TEST_F(RtcTimeTest, LessThan)
{
    // hours priority
    EXPECT_LT(rtc_time_t(10, 59, 59), rtc_time_t(11, 0, 0));

    // minutes priority
    EXPECT_LT(rtc_time_t(12, 29, 59), rtc_time_t(12, 30, 0));

    // seconds priority
    EXPECT_LT(rtc_time_t(12, 30, 44), rtc_time_t(12, 30, 45));

    // equal is not less
    EXPECT_FALSE(rtc_time_t(12, 30, 45) < rtc_time_t(12, 30, 45));
}

TEST_F(RtcTimeTest, LessEqual)
{
    EXPECT_LE(rtc_time_t(10, 0, 0), rtc_time_t(10, 0, 0));
    EXPECT_LE(rtc_time_t(10, 0, 0), rtc_time_t(10, 0, 1));
    EXPECT_FALSE(rtc_time_t(10, 0, 1) <= rtc_time_t(10, 0, 0));
}

TEST_F(RtcTimeTest, GreaterThan)
{
    EXPECT_GT(rtc_time_t(23, 59, 59), rtc_time_t(23, 59, 58));
    EXPECT_GT(rtc_time_t(23, 59, 59), rtc_time_t(0, 0, 0));
    EXPECT_FALSE(rtc_time_t(12, 30, 45) > rtc_time_t(12, 30, 45));
}

TEST_F(RtcTimeTest, GreaterEqual)
{
    EXPECT_GE(rtc_time_t(12, 30, 45), rtc_time_t(12, 30, 45));
    EXPECT_GE(rtc_time_t(12, 30, 46), rtc_time_t(12, 30, 45));
    EXPECT_FALSE(rtc_time_t(12, 30, 44) >= rtc_time_t(12, 30, 45));
}

TEST_F(RtcTimeTest, ComparisonEdgeCases)
{
    // Negative values (-1 = disabled fields)
    rtc_time_t neg(-1, -1, -1);
    rtc_time_t zero(0, 0, 0);
    EXPECT_LT(neg, zero);
    EXPECT_NE(neg, zero);
}

// ============================================================
// rtc_date_t
// ============================================================

class RtcDateTest : public ::testing::Test {};

TEST_F(RtcDateTest, DefaultConstructor)
{
    rtc_date_t d;
    EXPECT_EQ(d.year, 2000);
    EXPECT_EQ(d.month, 1);
    EXPECT_EQ(d.date, -1);
    EXPECT_EQ(d.weekDay, -1);
}

TEST_F(RtcDateTest, ParameterizedConstructor)
{
    rtc_date_t d(2026, 2, 26, 4);
    EXPECT_EQ(d.year, 2026);
    EXPECT_EQ(d.month, 2);
    EXPECT_EQ(d.date, 26);
    EXPECT_EQ(d.weekDay, 4);
}

TEST_F(RtcDateTest, ConstructFromTm)
{
    struct tm src{};
    src.tm_year = 126;  // 2026
    src.tm_mon  = 1;    // February
    src.tm_mday = 26;
    src.tm_wday = 4;  // Thursday

    rtc_date_t d(src);
    EXPECT_EQ(d.year, 2026);
    EXPECT_EQ(d.month, 2);
    EXPECT_EQ(d.date, 26);
    EXPECT_EQ(d.weekDay, 4);
}

TEST_F(RtcDateTest, FromTm)
{
    struct tm src{};
    src.tm_year = 99;  // 1999
    src.tm_mon  = 11;  // December
    src.tm_mday = 31;
    src.tm_wday = 5;

    auto d = rtc_date_t::from_tm(src);
    EXPECT_EQ(d.year, 1999);
    EXPECT_EQ(d.month, 12);
    EXPECT_EQ(d.date, 31);
    EXPECT_EQ(d.weekDay, 5);
}

TEST_F(RtcDateTest, ToTm)
{
    rtc_date_t d(2026, 2, 26, 4);
    struct tm result = d.to_tm();
    EXPECT_EQ(result.tm_year, 126);
    EXPECT_EQ(result.tm_mon, 1);
    EXPECT_EQ(result.tm_mday, 26);
    EXPECT_EQ(result.tm_wday, 4);
}

TEST_F(RtcDateTest, RoundTripTm)
{
    rtc_date_t original(2026, 6, 15, 1);
    struct tm mid   = original.to_tm();
    rtc_date_t back = rtc_date_t::from_tm(mid);
    EXPECT_EQ(original, back);
    EXPECT_EQ(back.weekDay, 1);
}

TEST_F(RtcDateTest, EqualityIgnoresWeekDay)
{
    rtc_date_t a(2026, 2, 26, 4);
    rtc_date_t b(2026, 2, 26, 0);
    EXPECT_EQ(a, b);
}

TEST_F(RtcDateTest, InequalityOperator)
{
    rtc_date_t a(2026, 2, 26, 4);

    // Differ by year
    EXPECT_NE(a, rtc_date_t(2025, 2, 26, 4));
    // Differ by month
    EXPECT_NE(a, rtc_date_t(2026, 3, 26, 4));
    // Differ by date
    EXPECT_NE(a, rtc_date_t(2026, 2, 27, 4));
}

TEST_F(RtcDateTest, LessThan)
{
    // Year priority
    EXPECT_LT(rtc_date_t(2025, 12, 31), rtc_date_t(2026, 1, 1));

    // Month priority
    EXPECT_LT(rtc_date_t(2026, 1, 31), rtc_date_t(2026, 2, 1));

    // Date priority
    EXPECT_LT(rtc_date_t(2026, 2, 25), rtc_date_t(2026, 2, 26));

    // weekDay ignored
    rtc_date_t a(2026, 2, 26, 0);
    rtc_date_t b(2026, 2, 26, 6);
    EXPECT_FALSE(a < b);
    EXPECT_FALSE(b < a);

    // Equal is not less
    EXPECT_FALSE(rtc_date_t(2026, 2, 26) < rtc_date_t(2026, 2, 26));
}

TEST_F(RtcDateTest, LessEqual)
{
    EXPECT_LE(rtc_date_t(2026, 2, 26), rtc_date_t(2026, 2, 26));
    EXPECT_LE(rtc_date_t(2026, 2, 25), rtc_date_t(2026, 2, 26));
    EXPECT_FALSE(rtc_date_t(2026, 2, 27) <= rtc_date_t(2026, 2, 26));
}

TEST_F(RtcDateTest, GreaterThan)
{
    EXPECT_GT(rtc_date_t(2026, 2, 26), rtc_date_t(2026, 2, 25));
    EXPECT_GT(rtc_date_t(2026, 2, 26), rtc_date_t(2025, 12, 31));
    EXPECT_FALSE(rtc_date_t(2026, 2, 26) > rtc_date_t(2026, 2, 26));
}

TEST_F(RtcDateTest, GreaterEqual)
{
    EXPECT_GE(rtc_date_t(2026, 2, 26), rtc_date_t(2026, 2, 26));
    EXPECT_GE(rtc_date_t(2026, 2, 27), rtc_date_t(2026, 2, 26));
    EXPECT_FALSE(rtc_date_t(2026, 2, 25) >= rtc_date_t(2026, 2, 26));
}

// ============================================================
// rtc_datetime_t
// ============================================================

class RtcDateTimeTest : public ::testing::Test {};

TEST_F(RtcDateTimeTest, DefaultConstructor)
{
    rtc_datetime_t dt;
    // date defaults
    EXPECT_EQ(dt.date.year, 2000);
    EXPECT_EQ(dt.date.month, 1);
    EXPECT_EQ(dt.date.date, -1);
    EXPECT_EQ(dt.date.weekDay, -1);
    // time defaults
    EXPECT_EQ(dt.time.hours, -1);
    EXPECT_EQ(dt.time.minutes, -1);
    EXPECT_EQ(dt.time.seconds, -1);
}

TEST_F(RtcDateTimeTest, ComponentConstructor)
{
    rtc_date_t d(2026, 2, 26, 4);
    rtc_time_t t(12, 30, 45);
    rtc_datetime_t dt(d, t);

    EXPECT_EQ(dt.date.year, 2026);
    EXPECT_EQ(dt.date.month, 2);
    EXPECT_EQ(dt.date.date, 26);
    EXPECT_EQ(dt.date.weekDay, 4);
    EXPECT_EQ(dt.time.hours, 12);
    EXPECT_EQ(dt.time.minutes, 30);
    EXPECT_EQ(dt.time.seconds, 45);
}

TEST_F(RtcDateTimeTest, ConstructFromTm)
{
    struct tm src{};
    src.tm_year = 126;  // 2026
    src.tm_mon  = 1;    // February
    src.tm_mday = 26;
    src.tm_wday = 4;
    src.tm_hour = 14;
    src.tm_min  = 20;
    src.tm_sec  = 33;

    rtc_datetime_t dt(src);
    EXPECT_EQ(dt.date.year, 2026);
    EXPECT_EQ(dt.date.month, 2);
    EXPECT_EQ(dt.date.date, 26);
    EXPECT_EQ(dt.date.weekDay, 4);
    EXPECT_EQ(dt.time.hours, 14);
    EXPECT_EQ(dt.time.minutes, 20);
    EXPECT_EQ(dt.time.seconds, 33);
}

TEST_F(RtcDateTimeTest, ToTm)
{
    rtc_date_t d(2026, 2, 26, 4);
    rtc_time_t t(14, 20, 33);
    rtc_datetime_t dt(d, t);

    struct tm result = dt.to_tm();
    EXPECT_EQ(result.tm_year, 126);
    EXPECT_EQ(result.tm_mon, 1);
    EXPECT_EQ(result.tm_mday, 26);
    EXPECT_EQ(result.tm_wday, 4);
    EXPECT_EQ(result.tm_hour, 14);
    EXPECT_EQ(result.tm_min, 20);
    EXPECT_EQ(result.tm_sec, 33);
    EXPECT_EQ(result.tm_isdst, -1);
}

TEST_F(RtcDateTimeTest, FromTm)
{
    struct tm src{};
    src.tm_year = 126;
    src.tm_mon  = 1;
    src.tm_mday = 26;
    src.tm_wday = 4;
    src.tm_hour = 14;
    src.tm_min  = 20;
    src.tm_sec  = 33;

    auto dt = rtc_datetime_t::from_tm(src);
    EXPECT_EQ(dt.date.year, 2026);
    EXPECT_EQ(dt.time.hours, 14);
}

TEST_F(RtcDateTimeTest, AssignFromTm)
{
    struct tm src{};
    src.tm_year = 126;
    src.tm_mon  = 1;
    src.tm_mday = 26;
    src.tm_wday = 4;
    src.tm_hour = 14;
    src.tm_min  = 20;
    src.tm_sec  = 33;

    rtc_datetime_t dt;
    dt = src;
    EXPECT_EQ(dt.date.year, 2026);
    EXPECT_EQ(dt.date.month, 2);
    EXPECT_EQ(dt.date.date, 26);
    EXPECT_EQ(dt.time.hours, 14);
    EXPECT_EQ(dt.time.minutes, 20);
    EXPECT_EQ(dt.time.seconds, 33);
}

TEST_F(RtcDateTimeTest, ExplicitTmConversion)
{
    rtc_date_t d(2026, 2, 26, 4);
    rtc_time_t t(14, 20, 33);
    rtc_datetime_t dt(d, t);

    struct tm result = static_cast<tm>(dt);
    EXPECT_EQ(result.tm_year, 126);
    EXPECT_EQ(result.tm_hour, 14);
    EXPECT_EQ(result.tm_isdst, -1);
}

TEST_F(RtcDateTimeTest, Equality)
{
    rtc_datetime_t a(rtc_date_t(2026, 2, 26, 4), rtc_time_t(12, 30, 45));
    rtc_datetime_t b(rtc_date_t(2026, 2, 26, 4), rtc_time_t(12, 30, 45));
    EXPECT_EQ(a, b);
}

TEST_F(RtcDateTimeTest, Inequality)
{
    rtc_datetime_t a(rtc_date_t(2026, 2, 26), rtc_time_t(12, 30, 45));

    // Different date
    rtc_datetime_t b(rtc_date_t(2026, 2, 27), rtc_time_t(12, 30, 45));
    EXPECT_NE(a, b);

    // Different time
    rtc_datetime_t c(rtc_date_t(2026, 2, 26), rtc_time_t(12, 30, 46));
    EXPECT_NE(a, c);
}

TEST_F(RtcDateTimeTest, LessThanDatePriority)
{
    // Earlier date, later time → still less
    rtc_datetime_t a(rtc_date_t(2026, 2, 25), rtc_time_t(23, 59, 59));
    rtc_datetime_t b(rtc_date_t(2026, 2, 26), rtc_time_t(0, 0, 0));
    EXPECT_LT(a, b);
}

TEST_F(RtcDateTimeTest, LessThanTimeFallback)
{
    // Same date, different time
    rtc_datetime_t a(rtc_date_t(2026, 2, 26), rtc_time_t(12, 30, 44));
    rtc_datetime_t b(rtc_date_t(2026, 2, 26), rtc_time_t(12, 30, 45));
    EXPECT_LT(a, b);
    EXPECT_FALSE(b < a);
}

TEST_F(RtcDateTimeTest, EqualityIgnoresWeekDay)
{
    rtc_datetime_t a(rtc_date_t(2026, 2, 26, 0), rtc_time_t(12, 0, 0));
    rtc_datetime_t b(rtc_date_t(2026, 2, 26, 6), rtc_time_t(12, 0, 0));
    EXPECT_EQ(a, b);
}

TEST_F(RtcDateTimeTest, LessEqual)
{
    rtc_datetime_t a(rtc_date_t(2026, 2, 26), rtc_time_t(12, 0, 0));
    rtc_datetime_t b(rtc_date_t(2026, 2, 26), rtc_time_t(12, 0, 0));
    EXPECT_LE(a, b);
    EXPECT_LE(a, rtc_datetime_t(rtc_date_t(2026, 2, 26), rtc_time_t(12, 0, 1)));
}

TEST_F(RtcDateTimeTest, GreaterThan)
{
    rtc_datetime_t a(rtc_date_t(2026, 2, 26), rtc_time_t(12, 0, 1));
    rtc_datetime_t b(rtc_date_t(2026, 2, 26), rtc_time_t(12, 0, 0));
    EXPECT_GT(a, b);
    EXPECT_FALSE(b > a);
}

TEST_F(RtcDateTimeTest, GreaterEqual)
{
    rtc_datetime_t a(rtc_date_t(2026, 2, 26), rtc_time_t(12, 0, 0));
    EXPECT_GE(a, a);
    EXPECT_GE(rtc_datetime_t(rtc_date_t(2026, 2, 27), rtc_time_t(0, 0, 0)), a);
}

// ============================================================
// TimerClock
// ============================================================

TEST(TimerClockTest, Values)
{
    EXPECT_EQ(static_cast<uint8_t>(TimerClock::Hz4096), 0);
    EXPECT_EQ(static_cast<uint8_t>(TimerClock::Hz64), 1);
    EXPECT_EQ(static_cast<uint8_t>(TimerClock::Hz1), 2);
    EXPECT_EQ(static_cast<uint8_t>(TimerClock::HzPM), 3);
}
