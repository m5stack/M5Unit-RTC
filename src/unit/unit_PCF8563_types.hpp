/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_PCF8563_types.hpp
  @brief Type definitions for PCF8563 Unit
 */
#ifndef M5_UNIT_RTC_UNIT_PCF8563_TYPES_HPP
#define M5_UNIT_RTC_UNIT_PCF8563_TYPES_HPP

#include <cstdint>
#include <tuple>
#include <time.h>

namespace m5 {
namespace unit {
namespace pcf8563 {

/*!
  @struct rtc_time_t
  @brief Time of day (hours, minutes, seconds)
 */
struct __attribute__((packed)) rtc_time_t {
    int8_t hours{};
    int8_t minutes{};
    int8_t seconds{};

    rtc_time_t(int8_t hours_ = -1, int8_t minutes_ = -1, int8_t seconds_ = -1)
        : hours{hours_}, minutes{minutes_}, seconds{seconds_}
    {
    }
    rtc_time_t(const tm& t) : hours{(int8_t)t.tm_hour}, minutes{(int8_t)t.tm_min}, seconds{(int8_t)t.tm_sec}
    {
    }
    //! @brief Construct from struct tm (time fields only)
    static inline rtc_time_t from_tm(const tm& t)
    {
        return rtc_time_t(t);
    }
    //! @brief Convert to struct tm (only time fields are set)
    inline tm to_tm() const
    {
        tm t{};
        t.tm_hour = hours;
        t.tm_min  = minutes;
        t.tm_sec  = seconds;
        return t;
    }
};

///@name rtc_time_t comparison operators
///@{
inline bool operator==(const rtc_time_t& lhs, const rtc_time_t& rhs)
{
    return lhs.hours == rhs.hours && lhs.minutes == rhs.minutes && lhs.seconds == rhs.seconds;
}
inline bool operator!=(const rtc_time_t& lhs, const rtc_time_t& rhs)
{
    return !(lhs == rhs);
}
inline bool operator<(const rtc_time_t& lhs, const rtc_time_t& rhs)
{
    return std::tie(lhs.hours, lhs.minutes, lhs.seconds) < std::tie(rhs.hours, rhs.minutes, rhs.seconds);
}
inline bool operator<=(const rtc_time_t& lhs, const rtc_time_t& rhs)
{
    return !(rhs < lhs);
}
inline bool operator>(const rtc_time_t& lhs, const rtc_time_t& rhs)
{
    return rhs < lhs;
}
inline bool operator>=(const rtc_time_t& lhs, const rtc_time_t& rhs)
{
    return !(lhs < rhs);
}
///@}

/*!
  @struct rtc_date_t
  @brief Calendar date (year, month, day, weekday)
 */
struct __attribute__((packed)) rtc_date_t {
    //! year 1900-2099
    int16_t year{};
    //! month 1-12
    int8_t month{};
    //! date 1-31
    int8_t date{};
    //! weekDay 0:sun / 1:mon / 2:tue / 3:wed / 4:thu / 5:fri / 6:sat
    int8_t weekDay{};

    rtc_date_t(int16_t year_ = 2000, int8_t month_ = 1, int8_t date_ = -1, int8_t weekDay_ = -1)
        : year{year_}, month{month_}, date{date_}, weekDay{weekDay_}
    {
    }
    rtc_date_t(const tm& t)
        : year{(int16_t)(t.tm_year + 1900)},
          month{(int8_t)(t.tm_mon + 1)},
          date{(int8_t)t.tm_mday},
          weekDay{(int8_t)t.tm_wday}
    {
    }
    //! @brief Construct from struct tm (date fields only)
    static inline rtc_date_t from_tm(const tm& t)
    {
        return rtc_date_t(t);
    }
    //! @brief Convert to struct tm (only date fields are set)
    inline tm to_tm() const
    {
        tm t{};
        t.tm_year = year - 1900;
        t.tm_mon  = month - 1;
        t.tm_mday = date;
        t.tm_wday = weekDay;
        return t;
    }
};

///@name rtc_date_t comparison operators (weekDay is excluded)
///@{
inline bool operator==(const rtc_date_t& lhs, const rtc_date_t& rhs)
{
    return lhs.year == rhs.year && lhs.month == rhs.month && lhs.date == rhs.date;
}
inline bool operator!=(const rtc_date_t& lhs, const rtc_date_t& rhs)
{
    return !(lhs == rhs);
}
inline bool operator<(const rtc_date_t& lhs, const rtc_date_t& rhs)
{
    return std::tie(lhs.year, lhs.month, lhs.date) < std::tie(rhs.year, rhs.month, rhs.date);
}
inline bool operator<=(const rtc_date_t& lhs, const rtc_date_t& rhs)
{
    return !(rhs < lhs);
}
inline bool operator>(const rtc_date_t& lhs, const rtc_date_t& rhs)
{
    return rhs < lhs;
}
inline bool operator>=(const rtc_date_t& lhs, const rtc_date_t& rhs)
{
    return !(lhs < rhs);
}
///@}

/*!
  @struct rtc_datetime_t
  @brief Combined date and time
 */
struct __attribute__((packed)) rtc_datetime_t {
    rtc_date_t date{};
    rtc_time_t time{};

    rtc_datetime_t() = default;
    rtc_datetime_t(const rtc_date_t& d, const rtc_time_t& t) : date{d}, time{t}
    {
    }
    rtc_datetime_t(const tm& t) : date{t}, time{t}
    {
    }
    //! @brief Assign from struct tm
    inline rtc_datetime_t& operator=(const tm& t)
    {
        date = rtc_date_t(t);
        time = rtc_time_t(t);
        return *this;
    }
    //! @brief Convert to struct tm (explicit)
    explicit inline operator tm() const
    {
        return to_tm();
    }
    //! @brief Construct from struct tm
    static inline rtc_datetime_t from_tm(const tm& t)
    {
        return rtc_datetime_t(t);
    }
    //! @brief Convert to struct tm
    inline tm to_tm() const
    {
        tm t{};
        t.tm_year  = date.year - 1900;
        t.tm_mon   = date.month - 1;
        t.tm_mday  = date.date;
        t.tm_wday  = date.weekDay;
        t.tm_hour  = time.hours;
        t.tm_min   = time.minutes;
        t.tm_sec   = time.seconds;
        t.tm_isdst = -1;
        return t;
    }
};

///@name rtc_datetime_t comparison operators (weekDay is excluded)
///@{
inline bool operator==(const rtc_datetime_t& lhs, const rtc_datetime_t& rhs)
{
    return lhs.date == rhs.date && lhs.time == rhs.time;
}
inline bool operator!=(const rtc_datetime_t& lhs, const rtc_datetime_t& rhs)
{
    return !(lhs == rhs);
}
inline bool operator<(const rtc_datetime_t& lhs, const rtc_datetime_t& rhs)
{
    if (lhs.date != rhs.date) {
        return lhs.date < rhs.date;
    }
    return lhs.time < rhs.time;
}
inline bool operator<=(const rtc_datetime_t& lhs, const rtc_datetime_t& rhs)
{
    return !(rhs < lhs);
}
inline bool operator>(const rtc_datetime_t& lhs, const rtc_datetime_t& rhs)
{
    return rhs < lhs;
}
inline bool operator>=(const rtc_datetime_t& lhs, const rtc_datetime_t& rhs)
{
    return !(lhs < rhs);
}
///@}

/*!
  @enum TimerClock
  @brief Timer clock source for PCF8563 countdown timer
 */
enum class TimerClock : uint8_t {
    Hz4096 = 0,  //!< 4.096 kHz (~244us resolution)
    Hz64   = 1,  //!< 64 Hz (~15.6ms resolution)
    Hz1    = 2,  //!< 1 Hz (1 second resolution)
    HzPM   = 3,  //!< 1/60 Hz (1 minute resolution)
};

/*!
  @enum ClockOutput
  @brief CLKOUT pin output frequency (register 0x0D)
 */
enum class ClockOutput : uint8_t {
    None    = 0x00,  //!< CLKOUT disabled (high-impedance)
    Hz32768 = 0x80,  //!< 32.768 kHz (FE=1, FD=00)
    Hz1024  = 0x81,  //!< 1.024 kHz (FE=1, FD=01)
    Hz32    = 0x82,  //!< 32 Hz (FE=1, FD=10)
    Hz1     = 0x83,  //!< 1 Hz (FE=1, FD=11)
};

}  // namespace pcf8563
}  // namespace unit
}  // namespace m5
#endif
