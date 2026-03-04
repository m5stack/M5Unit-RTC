/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_PCF8563.cpp
  @brief PCF8563 Unit for M5UnitUnified
 */

// Include M5Unified (or at least RTC_Base) before own header so that the
// M5Unified compat API guard is satisfied when unit_PCF8563.hpp is processed.
#if __has_include(<M5Unified.hpp>)
#include <M5Unified.hpp>
#elif __has_include(<utility/rtc/RTC_Base.hpp>)
#include <utility/rtc/RTC_Base.hpp>
#endif

#include "unit_PCF8563.hpp"
#include <M5Utility.hpp>
#include <driver/gpio.h>
#if __has_include(<sys/time.h>)
#include <sys/time.h>
#endif
#include <time.h>

using namespace m5::utility::mmh3;
using namespace m5::unit::types;
using namespace m5::unit::pcf8563;
using namespace m5::unit::pcf8563::command;

namespace {

inline uint8_t bcd2byte(const uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

inline uint8_t byte2bcd(const uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

// I2C stop condition between address-set and data phases
// true  = STOP + START (two separate transactions)
// false = RESTART (repeated start, single transaction)
constexpr bool READ_STOP{true};
constexpr bool WRITE_STOP{true};

}  // namespace

namespace m5 {
namespace unit {

const char UnitPCF8563::name[] = "UnitPCF8563";
const types::uid_t UnitPCF8563::uid{"UnitPCF8563"_mmh3};
const types::attr_t UnitPCF8563::attr{attribute::AccessI2C};

// ISR: just set the flag (no I2C here)
void IRAM_ATTR UnitPCF8563::isr_handler(void* arg)
{
    auto* self       = static_cast<UnitPCF8563*>(arg);
    self->_isr_fired = true;
}

bool UnitPCF8563::begin()
{
    // Clear control/status registers
    if (!write_register8(CONTROL1_REG, 0x00)) {
        M5_LIB_LOGE("Failed to write CONTROL1");
        return false;
    }
    if (!write_register8(CONTROL2_REG, 0x00)) {
        M5_LIB_LOGE("Failed to write CONTROL2");
        return false;
    }
    // Set CLKOUT from config (default: disabled for power saving)
    if (!write_register8(CLKOUT_CONTROL_REG, static_cast<uint8_t>(_cfg.clkout))) {
        M5_LIB_LOGE("Failed to write CLKOUT_CONTROL");
        return false;
    }
    // Disable timer (TE=0, TD=11 for minimum power)
    if (!write_register8(TIMER_CONTROL_REG, 0x03)) {
        M5_LIB_LOGE("Failed to write TIMER_CONTROL");
        return false;
    }

    // Setup hardware interrupt if configured
    if (!_cfg.polling && _cfg.int_pin >= 0) {
        gpio_num_t pin = static_cast<gpio_num_t>(_cfg.int_pin);
        gpio_config_t io_conf{};
        io_conf.pin_bit_mask = 1ULL << pin;
        io_conf.mode         = GPIO_MODE_INPUT;
        io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type    = GPIO_INTR_NEGEDGE;
        gpio_config(&io_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin, isr_handler, this);
        M5_LIB_LOGI("IRQ: hardware interrupt on pin %d", _cfg.int_pin);
    }

    // Set polling interval
    if (_cfg.polling && (_cfg.on_alarm || _cfg.on_timer)) {
        _interval = _cfg.polling_interval;
        _latest   = 0;
    }

    return true;
}

void UnitPCF8563::update(const bool force)
{
    // Skip if no callbacks configured
    if (!_cfg.on_alarm && !_cfg.on_timer) {
        return;
    }

    if (_cfg.polling) {
        // Polling mode: check IRQ status via I2C at interval
        auto at = m5::utility::millis();
        if (force || !_latest || at >= _latest + _interval) {
            _latest = at;
            check_irq_flags();
        }
    } else {
        // Hardware interrupt mode: check volatile flag set by ISR
        if (_isr_fired) {
            _isr_fired = false;
            check_irq_flags();
        }
    }
}

void UnitPCF8563::check_irq_flags()
{
    uint8_t ctrl2{};
    if (!read_register8(CONTROL2_REG, ctrl2)) {
        return;
    }

    bool alarm_flag = (ctrl2 & 0x08) != 0;
    bool timer_flag = (ctrl2 & 0x04) != 0;

    if (!alarm_flag && !timer_flag) {
        return;
    }

    // Clear fired flags (AF, TF) and reserved bits 7-5 (must be 0).
    // If clear fails, skip callbacks — flags remain set and will be
    // retried on the next poll, avoiding duplicate callbacks.
    if (!write_register8(CONTROL2_REG, ctrl2 & 0x13)) {
        M5_LIB_LOGW("Failed to clear IRQ flags");
        return;
    }

    if (alarm_flag && _cfg.on_alarm) {
        _cfg.on_alarm();
    }
    if (timer_flag) {
        // In oneshot mode (TI_TP=0), disable timer to prevent re-firing.
        // The PCF8563 counter always reloads regardless of TI_TP; TI_TP only
        // controls INT pin behavior. Without disabling TE, TF would be set
        // again on the next countdown cycle.
        if (!(ctrl2 & 0x10)) {  // TI_TP bit
            if (!writeTimerInterrupt(false) || !writeTimerControl(false, pcf8563::TimerClock::HzPM)) {
                M5_LIB_LOGW("Failed to disable oneshot timer");
            }
        }
        if (_cfg.on_timer) {
            _cfg.on_timer();
        }
    }
}

// ============================================================
// Protected helpers
// ============================================================

bool UnitPCF8563::read_register(const uint8_t reg, uint8_t* buf, const size_t len)
{
    return readRegister(reg, buf, len, 0, READ_STOP);
}

bool UnitPCF8563::read_register8(const uint8_t reg, uint8_t& val)
{
    return readRegister8(reg, val, 0, READ_STOP);
}

bool UnitPCF8563::write_register(const uint8_t reg, const uint8_t* buf, const size_t len)
{
    return writeRegister(reg, buf, len, WRITE_STOP);
}

bool UnitPCF8563::write_register8(const uint8_t reg, const uint8_t val)
{
    return writeRegister8(reg, val, WRITE_STOP);
}

bool UnitPCF8563::read_datetime(pcf8563::rtc_date_t* date, pcf8563::rtc_time_t* time)
{
    uint8_t start_reg = (time != nullptr) ? SECONDS_REG : DAYS_REG;
    size_t len        = ((date != nullptr) ? 4 : 0) + ((time != nullptr) ? 3 : 0);
    if (len == 0) {
        return false;
    }

    uint8_t buf[7]{};
    if (!read_register(start_reg, buf, len)) {
        M5_LIB_LOGE("Failed to read datetime");
        return false;
    }

    size_t idx = 0;
    if (time) {
        time->seconds = bcd2byte(buf[idx++] & 0x7F);
        time->minutes = bcd2byte(buf[idx++] & 0x7F);
        time->hours   = bcd2byte(buf[idx++] & 0x3F);
    }
    if (date) {
        date->date    = bcd2byte(buf[idx++] & 0x3F);
        date->weekDay = bcd2byte(buf[idx++] & 0x07);
        date->month   = bcd2byte(buf[idx++] & 0x1F);
        date->year    = bcd2byte(buf[idx] & 0xFF) + ((buf[idx - 1] & 0x80) ? 1900 : 2000);
    }
    return true;
}

bool UnitPCF8563::write_datetime(const pcf8563::rtc_date_t* date, const pcf8563::rtc_time_t* time)
{
    uint8_t buf[7]{};
    size_t idx        = 0;
    uint8_t reg_start = DAYS_REG;

    if (time) {
        reg_start  = SECONDS_REG;
        buf[idx++] = byte2bcd(time->seconds);
        buf[idx++] = byte2bcd(time->minutes);
        buf[idx++] = byte2bcd(time->hours);
    }
    if (date) {
        buf[idx++] = byte2bcd(date->date);
        buf[idx++] = static_cast<uint8_t>(0x07 & date->weekDay);
        buf[idx++] = static_cast<uint8_t>(byte2bcd(date->month) + (date->year < 2000 ? 0x80 : 0));
        buf[idx++] = byte2bcd(date->year % 100);
    }
    if (idx == 0) {
        return false;
    }
    if (!write_register(reg_start, buf, idx)) {
        M5_LIB_LOGE("Failed to write datetime");
        return false;
    }
    return true;
}

bool UnitPCF8563::read_control2(uint8_t& val)
{
    return read_register8(CONTROL2_REG, val);
}

bool UnitPCF8563::write_control2_bits(const uint8_t mask, const uint8_t bits)
{
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    val = ((val & ~mask) | (bits & mask)) & 0x1F;  // bits 7-5 are reserved (must be 0)
    return write_register8(CONTROL2_REG, val);
}

// ============================================================
// M5UnitUnified style API
// ============================================================

// ---- Date/Time ----

bool UnitPCF8563::readTime(pcf8563::rtc_time_t& time)
{
    time = {};
    return read_datetime(nullptr, &time);
}

bool UnitPCF8563::writeTime(const pcf8563::rtc_time_t& time)
{
    return write_datetime(nullptr, &time);
}

bool UnitPCF8563::readDate(pcf8563::rtc_date_t& date)
{
    date = {};
    return read_datetime(&date, nullptr);
}

bool UnitPCF8563::writeDate(const pcf8563::rtc_date_t& date)
{
    return write_datetime(&date, nullptr);
}

bool UnitPCF8563::readDateTime(pcf8563::rtc_datetime_t& dt)
{
    dt = pcf8563::rtc_datetime_t{};
    return read_datetime(&dt.date, &dt.time);
}

bool UnitPCF8563::writeDateTime(const pcf8563::rtc_datetime_t& dt)
{
    return write_datetime(&dt.date, &dt.time);
}

bool UnitPCF8563::readDateTime(struct tm& t)
{
    t = {};
    pcf8563::rtc_datetime_t dt{};
    if (!readDateTime(dt)) {
        return false;
    }
    t = dt.to_tm();
    return true;
}

bool UnitPCF8563::writeDateTime(const struct tm& t)
{
    return writeDateTime(pcf8563::rtc_datetime_t(t));
}

// ---- Alarm ----

bool UnitPCF8563::writeAlarm(const pcf8563::rtc_time_t& time, const pcf8563::rtc_date_t& date)
{
    bool has_alarm = (time.minutes >= 0 || time.hours >= 0 || date.date >= 0 || date.weekDay >= 0);

    // Read CONTROL2 once, then batch-modify AF and AIE
    uint8_t ctrl2{};
    if (!read_control2(ctrl2)) {
        return false;
    }
    ctrl2 &= ~0x0A;  // Clear AF(0x08), AIE(0x02)
    if (has_alarm) {
        ctrl2 |= 0x02;  // AIE
    }
    ctrl2 &= 0x1F;  // bits 7-5 are reserved (must be 0)

    uint8_t buf[4];
    buf[0] = (time.minutes < 0) ? 0x80 : static_cast<uint8_t>(byte2bcd(time.minutes) & 0x7F);
    buf[1] = (time.hours < 0) ? 0x80 : static_cast<uint8_t>(byte2bcd(time.hours) & 0x3F);
    buf[2] = (date.date < 0) ? 0x80 : static_cast<uint8_t>(byte2bcd(date.date) & 0x3F);
    buf[3] = (date.weekDay < 0) ? 0x80 : static_cast<uint8_t>(byte2bcd(date.weekDay) & 0x07);
    if (!write_register(ALARM_MINUTES_REG, buf, 4)) {
        M5_LIB_LOGE("Failed to write alarm registers");
        return false;
    }

    return write_register8(CONTROL2_REG, ctrl2);
}

bool UnitPCF8563::readAlarm(pcf8563::rtc_time_t& time, pcf8563::rtc_date_t& date)
{
    time = {};
    date = {};
    uint8_t buf[4]{};
    if (!read_register(ALARM_MINUTES_REG, buf, 4)) {
        M5_LIB_LOGE("Failed to read alarm registers");
        return false;
    }
    // Minutes alarm
    time.minutes = (buf[0] & 0x80) ? -1 : static_cast<int8_t>(bcd2byte(buf[0] & 0x7F));
    // Hours alarm
    time.hours = (buf[1] & 0x80) ? -1 : static_cast<int8_t>(bcd2byte(buf[1] & 0x3F));
    // Seconds not part of alarm
    time.seconds = -1;
    // Day alarm
    date.date = (buf[2] & 0x80) ? -1 : static_cast<int8_t>(bcd2byte(buf[2] & 0x3F));
    // Weekday alarm
    date.weekDay = (buf[3] & 0x80) ? -1 : static_cast<int8_t>(bcd2byte(buf[3] & 0x07));
    // Year and month not part of alarm
    date.year  = 2000;
    date.month = 1;
    return true;
}

bool UnitPCF8563::readAlarmInterrupt(bool& enabled)
{
    enabled = false;
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    enabled = (val & 0x02) != 0;  // AIE bit
    return true;
}

bool UnitPCF8563::writeAlarmInterrupt(const bool enabled)
{
    return write_control2_bits(0x02, enabled ? 0x02 : 0x00);
}

bool UnitPCF8563::readAlarmFlag(bool& fired)
{
    fired = false;
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    fired = (val & 0x08) != 0;  // AF bit
    return true;
}

bool UnitPCF8563::clearAlarmFlag()
{
    return write_control2_bits(0x08, 0x00);
}

// ---- Timer ----

uint32_t UnitPCF8563::writeTimer(const uint32_t msec, const bool repeat)
{
    // Read CONTROL2 once, then batch-modify AF, TF, TI_TP, TIE
    uint8_t ctrl2{};
    if (!read_control2(ctrl2)) {
        return 0;
    }
    // Clear AF(0x08), TF(0x04), TI_TP(0x10), TIE(0x01) and reserved bits 7-5
    ctrl2 &= ~0x1D;
    ctrl2 &= 0x1F;

    uint32_t afterSeconds = (msec + 500) / 1000;
    if (afterSeconds == 0) {
        // Disable: write CONTROL2 (flags cleared, TIE=0, TI_TP=0) + disable timer
        if (!write_register8(CONTROL2_REG, ctrl2) || !writeTimerControl(false, pcf8563::TimerClock::HzPM)) {
            M5_LIB_LOGW("Failed to disable timer");
        }
        return 0;
    }

    uint32_t div              = 1;
    pcf8563::TimerClock clock = pcf8563::TimerClock::Hz1;
    if (afterSeconds < 270) {
        if (afterSeconds > 255) {
            afterSeconds = 255;
        }
    } else {
        div          = 60;
        afterSeconds = (afterSeconds + 30) / div;
        if (afterSeconds > 255) {
            afterSeconds = 255;
        }
        clock = pcf8563::TimerClock::HzPM;
    }

    // Set TI_TP if repeat, set TIE
    if (repeat) {
        ctrl2 |= 0x10;
    }
    ctrl2 |= 0x01;  // TIE
    if (!write_register8(CONTROL2_REG, ctrl2)) {
        return 0;
    }

    // Disable timer first, then set countdown value, then enable.
    // Writing TE=1 and countdown as consecutive bytes (0x0E, 0x0F) risks the
    // timer starting with the OLD countdown value before 0x0F is updated.
    uint8_t td = static_cast<uint8_t>(clock) & 0x03;
    if (!write_register8(TIMER_CONTROL_REG, td)                             // TE=0 + TD (stop)
        || !write_register8(TIMER_REG, static_cast<uint8_t>(afterSeconds))  // countdown value
        || !write_register8(TIMER_CONTROL_REG, 0x80 | td)) {                // TE=1 + TD (start)
        // Timer may be in inconsistent state; disable it
        write_register8(TIMER_CONTROL_REG, 0x03);  // TE=0, TD=11 (low power)
        return 0;
    }

    return afterSeconds * div * 1000;
}

bool UnitPCF8563::readTimerControl(bool& enabled, pcf8563::TimerClock& clock)
{
    enabled = false;
    clock   = pcf8563::TimerClock::Hz4096;
    uint8_t val{};
    if (!read_register8(TIMER_CONTROL_REG, val)) {
        return false;
    }
    enabled = (val & 0x80) != 0;                             // TE bit
    clock   = static_cast<pcf8563::TimerClock>(val & 0x03);  // TD bits
    return true;
}

bool UnitPCF8563::writeTimerControl(const bool enabled, const pcf8563::TimerClock clock)
{
    uint8_t val = (enabled ? 0x80 : 0x00) | (static_cast<uint8_t>(clock) & 0x03);
    return write_register8(TIMER_CONTROL_REG, val);
}

bool UnitPCF8563::readTimerValue(uint8_t& count)
{
    count = 0;
    return read_register8(TIMER_REG, count);
}

bool UnitPCF8563::writeTimerValue(const uint8_t count)
{
    return write_register8(TIMER_REG, count);
}

bool UnitPCF8563::readTimerInterrupt(bool& enabled)
{
    enabled = false;
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    enabled = (val & 0x01) != 0;  // TIE bit
    return true;
}

bool UnitPCF8563::writeTimerInterrupt(const bool enabled)
{
    return write_control2_bits(0x01, enabled ? 0x01 : 0x00);
}

bool UnitPCF8563::readTimerFlag(bool& fired)
{
    fired = false;
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    fired = (val & 0x04) != 0;  // TF bit
    return true;
}

bool UnitPCF8563::clearTimerFlag()
{
    return write_control2_bits(0x04, 0x00);
}

bool UnitPCF8563::readTimerPeriodic(bool& periodic)
{
    periodic = false;
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    periodic = (val & 0x10) != 0;  // TI_TP bit
    return true;
}

bool UnitPCF8563::writeTimerPeriodic(const bool periodic)
{
    return write_control2_bits(0x10, periodic ? 0x10 : 0x00);
}

// ---- Clock Control ----

bool UnitPCF8563::readStop(bool& stopped)
{
    stopped = false;
    uint8_t val{};
    if (!read_register8(CONTROL1_REG, val)) {
        return false;
    }
    stopped = (val & 0x20) != 0;  // STOP bit
    return true;
}

bool UnitPCF8563::writeStop(const bool stop)
{
    // Only STOP bit (5) is valid; TEST1/TESTC and reserved bits must be 0
    return write_register8(CONTROL1_REG, stop ? 0x20 : 0x00);
}

// ---- Status ----

bool UnitPCF8563::readVoltLow(bool& low)
{
    low = false;
    uint8_t val{};
    if (!read_register8(SECONDS_REG, val)) {
        return false;
    }
    low = (val & 0x80) != 0;  // VL bit
    return true;
}

// ============================================================
// M5Unified compatible API
// ============================================================

#if defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

int UnitPCF8563::setAlarmIRQ(const m5::rtc_date_t* date, const m5::rtc_time_t* time)
{
    pcf8563::rtc_time_t at{-1, -1, -1};
    pcf8563::rtc_date_t ad{2000, 1, -1, -1};

    if (time) {
        if (time->minutes >= 0) {
            at.minutes = time->minutes;
        }
        if (time->hours >= 0) {
            at.hours = time->hours;
        }
    }
    if (date) {
        if (date->date >= 0) {
            ad.date = date->date;
        }
        if (date->weekDay >= 0) {
            ad.weekDay = date->weekDay;
        }
    }

    return writeAlarm(at, ad) ? (at.minutes >= 0 || at.hours >= 0 || ad.date >= 0 || ad.weekDay >= 0 ? 1 : 0) : 0;
}

int UnitPCF8563::setAlarmIRQ(const int afterSeconds)
{
    if (afterSeconds < 0) {
        setTimerIRQ(0);
        return -1;
    }
    return static_cast<int>(setTimerIRQ(static_cast<uint32_t>(afterSeconds) * 1000) / 1000);
}

uint32_t UnitPCF8563::setTimerIRQ(const uint32_t timer_msec)
{
    return writeTimer(timer_msec);
}

void UnitPCF8563::disableIRQ()
{
    // Disable alarm (all fields disabled)
    pcf8563::rtc_time_t at{-1, -1, -1};
    pcf8563::rtc_date_t ad{2000, 1, -1, -1};
    writeAlarm(at, ad);
    // Disable timer
    writeTimerControl(false, pcf8563::TimerClock::Hz1);
    // Clear all CONTROL2 flags and enable bits
    write_register8(CONTROL2_REG, 0x00);
}

void UnitPCF8563::setSystemTimeFromRtc(struct timezone* tz)
{
    pcf8563::rtc_datetime_t dt;
    if (!readDateTime(dt)) {
        return;
    }
    struct tm t = dt.to_tm();

    struct timeval tv{};
    tv.tv_sec = mktime(&t);
    settimeofday(&tv, tz);
}

#endif  // defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

}  // namespace unit
}  // namespace m5
