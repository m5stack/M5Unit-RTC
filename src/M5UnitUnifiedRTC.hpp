/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file M5UnitUnifiedRTC.hpp
  @brief Main header of M5UnitRTC using M5UnitUnified

  @mainpage M5Unit-RTC
  Library for Unit-RTC using M5UnitUnified.
*/
#if defined(_Unit_RTC_H__)
#error "DO NOT USE it at the same time as conventional libraries"
#endif

#ifndef M5_UNIT_UNIFIED_RTC_HPP
#define M5_UNIT_UNIFIED_RTC_HPP

#include "unit/unit_PCF8563.hpp"
#include "unit/unit_RX8130.hpp"

namespace m5 {
namespace unit {
using UnitBM8563   = m5::unit::UnitPCF8563;  //!< @brief Alias of UnitPCF8563 for the BM8563
using UnitHYM8563  = m5::unit::UnitPCF8563;  //!< @brief Alias of UnitPCF8563 for the HYM8563
using UnitRTC      = m5::unit::UnitPCF8563;  //!< @brief Alias of UnitPCF8563 for Unit RTC (SKU:U126)
using UnitRX8130CE = m5::unit::UnitRX8130;   //!< @brief Alias of UnitRX8130 for the RX8130CE

/*!
  @class Tab5BuiltinRTC
  @brief M5Stack Tab5 built-in RTC (RX8130CE on the internal I2C bus)
  @details UnitRX8130 subclass for the Tab5. The /IRQ of the chip drives the power management MCU of the Tab5,
  so the periodic interrupts, which can make the Tab5 unbootable, are refused (see the IRQ notes of UnitRX8130):
  - writeTimerInterrupt(true) / writeUpdateInterrupt(true) return false
  - setTimerIRQ() / setAlarmIRQ(afterSeconds) with a positive period stop the timer and return 0, as M5Unified
  The alarm interrupt is allowed: it turns the Tab5 on while it is off (the wakeup of M5Unified timerSleep(time)).
  config_t::charge is true by default (the backup is a supercapacitor through 1 kOhm).
 */
class Tab5BuiltinRTC : public UnitRX8130 {
public:
    explicit Tab5BuiltinRTC(const uint8_t addr = DEFAULT_ADDRESS) : UnitRX8130(addr)
    {
        _periodic_irq = false;
        auto cfg      = config();
        cfg.charge    = true;  // Tab5 built-in backup: supercapacitor through 1 kOhm
        config(cfg);
    }
};
}  // namespace unit
}  // namespace m5
#endif
