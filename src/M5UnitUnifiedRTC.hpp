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

using UnitBM8563  = m5::unit::UnitPCF8563;
using UnitHYM8563 = m5::unit::UnitPCF8563;
using UnitRTC     = m5::unit::UnitPCF8563;  //!< @brief Alias for UnitRTC

#endif
