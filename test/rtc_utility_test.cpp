/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  UnitTest for the shared RTC helpers (host and boards)
*/
#include <gtest/gtest.h>
#include <utility/rtc_utility.hpp>

using namespace m5::unit::rtc;
using m5::unit::pcf8563::rtc_date_t;
using m5::unit::pcf8563::rtc_time_t;

TEST(RtcUtility, Bcd)
{
    for (uint8_t v = 0; v < 100; ++v) {
        const uint8_t bcd = byte2bcd(v);
        EXPECT_TRUE(is_valid_bcd(bcd)) << (int)v;
        EXPECT_EQ(bcd2byte(bcd), v);
    }
    EXPECT_EQ(byte2bcd(59), 0x59);
    EXPECT_EQ(bcd2byte(0x23), 23);
    EXPECT_FALSE(is_valid_bcd(0x0A));
    EXPECT_FALSE(is_valid_bcd(0xA0));
    EXPECT_FALSE(is_valid_bcd(0xFF));
}

TEST(RtcUtility, DayOfWeek)
{
    EXPECT_EQ(day_of_week(2026, 10, 22), 4);  // Thursday
    EXPECT_EQ(day_of_week(2000, 1, 1), 6);    // Saturday (January branch)
    EXPECT_EQ(day_of_week(2000, 2, 29), 2);   // Tuesday (leap day, February branch)
    EXPECT_EQ(day_of_week(2024, 2, 29), 4);   // Thursday
    EXPECT_EQ(day_of_week(2099, 12, 31), 4);  // Thursday
    EXPECT_EQ(day_of_week(1999, 12, 31), 5);  // Friday
    EXPECT_EQ(day_of_week(1900, 1, 1), 1);    // Monday (1900 is not a leap year)
    EXPECT_EQ(day_of_week(1900, 3, 1), 4);    // Thursday
}

TEST(RtcUtility, ValidTime)
{
    EXPECT_TRUE(is_valid_time(rtc_time_t(0, 0, 0)));
    EXPECT_TRUE(is_valid_time(rtc_time_t(23, 59, 59)));
    EXPECT_FALSE(is_valid_time(rtc_time_t(24, 0, 0)));
    EXPECT_FALSE(is_valid_time(rtc_time_t(0, 60, 0)));
    EXPECT_FALSE(is_valid_time(rtc_time_t(0, 0, 60)));
    EXPECT_FALSE(is_valid_time(rtc_time_t(-1, 0, 0)));
}

TEST(RtcUtility, ValidDate)
{
    EXPECT_TRUE(is_valid_date(rtc_date_t(2000, 1, 1, 6), 2000, 2099));
    EXPECT_TRUE(is_valid_date(rtc_date_t(2099, 12, 31, -1), 2000, 2099));  // negative weekDay is allowed
    EXPECT_FALSE(is_valid_date(rtc_date_t(1999, 12, 31, 5), 2000, 2099));
    EXPECT_TRUE(is_valid_date(rtc_date_t(1999, 12, 31, 5), 1900, 2099));
    EXPECT_FALSE(is_valid_date(rtc_date_t(2100, 1, 1, 5), 1900, 2099));
    EXPECT_FALSE(is_valid_date(rtc_date_t(2026, 0, 1, 0), 2000, 2099));
    EXPECT_FALSE(is_valid_date(rtc_date_t(2026, 13, 1, 0), 2000, 2099));
    EXPECT_FALSE(is_valid_date(rtc_date_t(2026, 1, 0, 0), 2000, 2099));
    EXPECT_FALSE(is_valid_date(rtc_date_t(2026, 1, 32, 0), 2000, 2099));
    EXPECT_FALSE(is_valid_date(rtc_date_t(2026, 1, 1, 7), 2000, 2099));
}
