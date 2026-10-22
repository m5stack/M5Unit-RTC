/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file rtc_utility.hpp
  @brief BCD and calendar helpers shared by the RTC units
*/
#ifndef M5_UNIT_RTC_UTILITY_RTC_UTILITY_HPP
#define M5_UNIT_RTC_UTILITY_RTC_UTILITY_HPP

#include "../unit/unit_PCF8563_types.hpp"
#include <cstdint>

namespace m5 {
namespace unit {
namespace rtc {

//! @brief BCD (0x00-0x99) to binary
constexpr uint8_t bcd2byte(const uint8_t bcd)
{
    return static_cast<uint8_t>(((bcd >> 4) * 10) + (bcd & 0x0F));
}

//! @brief Binary (0-99) to BCD
constexpr uint8_t byte2bcd(const uint8_t val)
{
    return static_cast<uint8_t>(((val / 10) << 4) | (val % 10));
}

//! @brief Is it a valid BCD (each digit 0-9)?
constexpr bool is_valid_bcd(const uint8_t bcd)
{
    return (bcd & 0x0F) <= 9 && (bcd >> 4) <= 9;
}

/*!
  @brief Day of the week by Sakamoto's method
  @param year Year (Gregorian)
  @param month Month 1-12
  @param date Day of the month 1-31
  @return 0:Sunday ... 6:Saturday
 */
inline int8_t day_of_week(const int16_t year, const int8_t month, const int8_t date)
{
    static constexpr int8_t table[]{0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int y = year - (month < 3);
    return static_cast<int8_t>((y + y / 4 - y / 100 + y / 400 + table[month - 1] + date) % 7);
}

//! @brief Are the time fields in range (hours 0-23, minutes 0-59, seconds 0-59)?
inline bool is_valid_time(const pcf8563::rtc_time_t& t)
{
    return t.hours >= 0 && t.hours <= 23 && t.minutes >= 0 && t.minutes <= 59 && t.seconds >= 0 && t.seconds <= 59;
}

/*!
  @brief Are the date fields in range?
  @param d Date (month 1-12, date 1-31, weekDay 0-6 or negative)
  @param min_year Minimum year of the RTC
  @param max_year Maximum year of the RTC
  @note The day of the month is not checked against the month
 */
inline bool is_valid_date(const pcf8563::rtc_date_t& d, const int16_t min_year, const int16_t max_year)
{
    return d.year >= min_year && d.year <= max_year && d.month >= 1 && d.month <= 12 && d.date >= 1 && d.date <= 31 &&
           d.weekDay <= 6;
}

}  // namespace rtc
}  // namespace unit
}  // namespace m5
#endif
