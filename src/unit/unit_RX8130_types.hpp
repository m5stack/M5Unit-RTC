/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_RX8130_types.hpp
  @brief Type definitions for RX8130CE
 */
#ifndef M5_UNIT_RTC_UNIT_RX8130_TYPES_HPP
#define M5_UNIT_RTC_UNIT_RX8130_TYPES_HPP

#include <cstdint>
#include "unit_PCF8563_types.hpp"

namespace m5 {
namespace unit {
namespace rx8130 {

///@name Date/Time types (same layout as PCF8563)
///@{
using rtc_time_t     = m5::unit::pcf8563::rtc_time_t;      //!< @brief Time of day
using rtc_date_t     = m5::unit::pcf8563::rtc_date_t;      //!< @brief Calendar date (year 2000-2099)
using rtc_datetime_t = m5::unit::pcf8563::rtc_datetime_t;  //!< @brief Combined date and time
///@}

/*!
  @enum TimerClock
  @brief Source clock of the wakeup timer (TSEL2-0)
 */
enum class TimerClock : uint8_t {
    Hz4096 = 0,  //!< 4096 Hz (244.14 us per count, /IRQ auto release 122 us)
    Hz64   = 1,  //!< 64 Hz (15.625 ms per count)
    Hz1    = 2,  //!< 1 Hz (1 second per count)
    HzPM   = 3,  //!< 1/60 Hz (1 minute per count)
    HzPH   = 4,  //!< 1/3600 Hz (1 hour per count)
};

/*!
  @enum TimerCountMode
  @brief Power source during which the wakeup timer counts (TBKE, TBKON)
 */
enum class TimerCountMode : uint8_t {
    Always     = 0,  //!< Counts in both main (VDD) and backup (VBAT) operation (TBKE=0)
    MainOnly   = 1,  //!< Counts only in main (VDD) operation (TBKE=1, TBKON=0)
    BackupOnly = 3,  //!< Counts only in backup (VBAT) operation (TBKE=1, TBKON=1)
};

/*!
  @enum UpdateTiming
  @brief Time update interrupt timing (USEL)
 */
enum class UpdateTiming : uint8_t {
    Second = 0,  //!< Every second update
    Minute = 1,  //!< Every minute update
};

/*!
  @enum ClockOutput
  @brief FOUT pin output frequency (FSEL1, FSEL0)
  @note FOUT is also gated by the FOE pin of the chip
 */
enum class ClockOutput : uint8_t {
    Hz32768 = 0,  //!< 32.768 kHz
    Hz1024  = 1,  //!< 1024 Hz
    Hz1     = 2,  //!< 1 Hz (not output while STOP=1)
    None    = 3,  //!< Output off
};

/*!
  @enum FullChargeVoltage
  @brief Full charge detection voltage of the backup battery (BFVSEL1, BFVSEL0)
 */
enum class FullChargeVoltage : uint8_t {
    V3_02     = 0,  //!< 3.02 V
    V3_08     = 1,  //!< 3.08 V
    V2_92     = 2,  //!< 2.92 V
    Unlimited = 3,  //!< No full charge detection (charging without limit)
};

/*!
  @enum ResetVoltage
  @brief VDD reset detection voltage VDET1 (RSVSEL)
 */
enum class ResetVoltage : uint8_t {
    V2_75 = 0,  //!< 2.75 V
    V2_70 = 1,  //!< 2.70 V
};

/*!
  @enum DetectionTime
  @brief SW1 off time for VBAT voltage detection (SMPTSEL1, SMPTSEL0)
 */
enum class DetectionTime : uint8_t {
    Ms2   = 0,  //!< 2 ms
    Ms16  = 1,  //!< 16 ms
    Ms128 = 2,  //!< 128 ms
    Ms256 = 3,  //!< 256 ms
};

///@name Digital offset
///@{
constexpr float DIGITAL_OFFSET_STEP_PPM{3.05f};  //!< @brief Digital offset step (ppm)
constexpr int8_t DIGITAL_OFFSET_MIN{-64};        //!< @brief Minimum step (-195.31 ppm)
constexpr int8_t DIGITAL_OFFSET_MAX{63};         //!< @brief Maximum step (+192.26 ppm)
///@}

}  // namespace rx8130
}  // namespace unit
}  // namespace m5
#endif
