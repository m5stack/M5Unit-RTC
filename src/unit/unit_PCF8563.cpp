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
#include "../utility/rtc_utility.hpp"
#include <M5Utility.hpp>
#include <driver/gpio.h>
#include <algorithm>
#include <utility>
#if __has_include(<sys/time.h>)
#include <sys/time.h>
#endif
#include <time.h>

using namespace m5::utility::mmh3;
using namespace m5::unit::types;
using namespace m5::unit::pcf8563;
using namespace m5::unit::pcf8563::command;

namespace {

using m5::unit::rtc::bcd2byte;
using m5::unit::rtc::byte2bcd;
using m5::unit::rtc::day_of_week;
using m5::unit::rtc::is_valid_bcd;
using m5::unit::rtc::is_valid_time;

// The century bit covers 1900-2099. A negative weekDay is calculated from the date
inline bool is_valid_date(const rtc_date_t& d)
{
    return m5::unit::rtc::is_valid_date(d, 1900, 2099);
}

// CONTROL2 bits
constexpr uint8_t CTRL2_TIE{0x01};
constexpr uint8_t CTRL2_AIE{0x02};
constexpr uint8_t CTRL2_TF{0x04};
constexpr uint8_t CTRL2_AF{0x08};
constexpr uint8_t CTRL2_TI_TP{0x10};
constexpr uint8_t CTRL2_WRITABLE{0x1F};  // bits 7-5 are reserved (must be 0)

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

UnitPCF8563::~UnitPCF8563()
{
    remove_isr();
}

UnitPCF8563::UnitPCF8563(UnitPCF8563&& other) noexcept
    : Component(std::move(other)), _cfg{other._cfg}, _isr_fired{other._isr_fired}, _isr_pin{other._isr_pin}
{
    // other no longer owns the handler, so its destructor does not remove it
    other._isr_pin = -1;
    rebind_isr();
    // An interrupt that came before the rebind set the flag of other
    if (other._isr_fired) {
        _isr_fired = true;
    }
    other._isr_fired = false;  // the ISR no longer writes to other
}

UnitPCF8563& UnitPCF8563::operator=(UnitPCF8563&& other) noexcept
{
    if (this != &other) {
        remove_isr();
        Component::operator=(std::move(other));
        _cfg               = other._cfg;
        _isr_fired         = other._isr_fired;
        _isr_pin           = other._isr_pin;
        other._isr_pin     = -1;
        rebind_isr();
        // An interrupt that came before the rebind set the flag of other
        if (other._isr_fired) {
            _isr_fired = true;
        }
        other._isr_fired = false;  // the ISR no longer writes to other
    }
    return *this;
}

void UnitPCF8563::remove_isr()
{
    if (_isr_pin >= 0) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(_isr_pin));
        _isr_pin = -1;
    }
}

void UnitPCF8563::rebind_isr()
{
    // gpio_isr_handler_add() replaces the handler and its argument of the pin under a spinlock.
    // The flag set on the moved-from unit before that is merged by the caller
    if (_isr_pin >= 0 && gpio_isr_handler_add(static_cast<gpio_num_t>(_isr_pin), isr_handler, this) != ESP_OK) {
        M5_LIB_LOGE("Failed to move the ISR handler on pin %d", _isr_pin);
        // Do not leave the handler pointing at the moved-from unit
        gpio_isr_handler_remove(static_cast<gpio_num_t>(_isr_pin));
        _isr_pin = -1;
    }
}

bool UnitPCF8563::begin()
{
    // Remove the handler of an earlier begin()
    remove_isr();
    _isr_fired = false;

    // Clear control/status registers
    if (!write_register8(CONTROL1_REG, 0x00)) {
        M5_LIB_LOGE("Failed to write CONTROL1");
        return false;
    }
    // Interrupts disabled, TI_TP=0. The flags (AF/TF) are kept so that the wakeup source can be read after begin
    if (!write_control2_bits(CTRL2_AIE | CTRL2_TIE | CTRL2_TI_TP, 0)) {
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
        const gpio_num_t pin{static_cast<gpio_num_t>(_cfg.int_pin)};
        gpio_config_t io_conf{};
        io_conf.pin_bit_mask = 1ULL << pin;
        io_conf.mode         = GPIO_MODE_INPUT;
        io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type    = GPIO_INTR_NEGEDGE;
        if (gpio_config(&io_conf) != ESP_OK) {
            M5_LIB_LOGE("Failed to configure pin %d", _cfg.int_pin);
            return false;
        }
        const auto err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // INVALID_STATE: already installed
            M5_LIB_LOGE("Failed to install ISR service %d", err);
            return false;
        }
        if (gpio_isr_handler_add(pin, isr_handler, this) != ESP_OK) {
            M5_LIB_LOGE("Failed to add ISR handler on pin %d", _cfg.int_pin);
            return false;
        }
        _isr_pin = _cfg.int_pin;
        M5_LIB_LOGI("IRQ: hardware interrupt on pin %d", _cfg.int_pin);
    }

    _interval = _cfg.polling_interval;
    _latest   = 0;
    return true;
}

void UnitPCF8563::update(const bool force)
{
    // Nothing to do without callbacks
    if (!_cfg.on_alarm && !_cfg.on_timer) {
        return;
    }

    if (_cfg.polling || _cfg.int_pin < 0) {
        // Polling mode: check IRQ status via I2C at interval
        auto at = m5::utility::millis();
        if (force || !_latest || m5::utility::hasElapsed(_latest, _interval, at)) {
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

    // Handle (and clear) only the flags that have a callback.
    // The others are left for readAlarmFlag() / readTimerFlag(), except in the hardware interrupt mode:
    // there a flag with its interrupt enabled is cleared too, as it would hold /INT low and no further
    // falling edge (the ISR trigger) would arrive
    uint8_t handled{static_cast<uint8_t>((_cfg.on_alarm ? CTRL2_AF : 0) | (_cfg.on_timer ? CTRL2_TF : 0))};
    if (!_cfg.polling && _cfg.int_pin >= 0) {
        handled |= ((ctrl2 & CTRL2_AIE) ? CTRL2_AF : 0) | ((ctrl2 & CTRL2_TIE) ? CTRL2_TF : 0);
    }
    const uint8_t fired{static_cast<uint8_t>(ctrl2 & handled)};
    const bool alarm_flag{(fired & CTRL2_AF) != 0};
    const bool timer_flag{(fired & CTRL2_TF) != 0};

    if (!fired) {
        return;
    }

    // Clear the handled flags only; the other flag is written back as read.
    // The datasheets say writing 1 to AF/TF leaves it unchanged, but on the M5Stack Unit RTC writing 1 sets the
    // flag (checked on the device), so a flag is never written as 1 unless it was read as 1.
    // If clear fails, skip callbacks — flags remain set and will be
    // retried on the next poll, avoiding duplicate callbacks.
    if (!write_register8(CONTROL2_REG, static_cast<uint8_t>((ctrl2 & ~fired) & CTRL2_WRITABLE))) {
        M5_LIB_LOGW("Failed to clear IRQ flags");
        return;
    }

    if (alarm_flag && _cfg.on_alarm) {
        _cfg.on_alarm();
    }
    if (timer_flag && _cfg.on_timer) {
        _cfg.on_timer();
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
    const uint8_t start_reg{(time != nullptr) ? SECONDS_REG : DAYS_REG};
    const size_t len{static_cast<size_t>(((date != nullptr) ? 4 : 0) + ((time != nullptr) ? 3 : 0))};
    if (len == 0) {
        return false;
    }

    uint8_t buf[7]{};
    if (!read_register(start_reg, buf, len)) {
        M5_LIB_LOGE("Failed to read datetime");
        return false;
    }

    // Reject corrupted BCD (e.g. after the backup voltage was lost)
    static constexpr uint8_t masks[7]{0x7F, 0x7F, 0x3F, 0x3F, 0x07, 0x1F, 0xFF};
    const size_t mask_start{(time != nullptr) ? 0U : 3U};
    for (size_t i = 0; i < len; ++i) {
        if (!is_valid_bcd(buf[i] & masks[mask_start + i])) {
            M5_LIB_LOGE("Invalid BCD in datetime");
            return false;
        }
    }

    size_t idx{};
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
    size_t idx{};
    uint8_t reg_start{DAYS_REG};

    if (time) {
        if (!is_valid_time(*time)) {
            M5_LIB_LOGE("Invalid time %d:%d:%d", time->hours, time->minutes, time->seconds);
            return false;
        }
        reg_start  = SECONDS_REG;
        buf[idx++] = byte2bcd(time->seconds);
        buf[idx++] = byte2bcd(time->minutes);
        buf[idx++] = byte2bcd(time->hours);
    }
    if (date) {
        if (!is_valid_date(*date)) {
            M5_LIB_LOGE("Invalid date %d-%d-%d W:%d", date->year, date->month, date->date, date->weekDay);
            return false;
        }
        const int8_t wd{(date->weekDay < 0) ? day_of_week(date->year, date->month, date->date) : date->weekDay};
        buf[idx++] = byte2bcd(date->date);
        buf[idx++] = static_cast<uint8_t>(wd);
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
    // Flags (AF/TF) not in mask are written back as read (writing 1 sets them on the Unit RTC)
    val = static_cast<uint8_t>(((val & ~mask) | (bits & mask)) & CTRL2_WRITABLE);
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
    // Negative fields are disabled; the others must be in range
    if (time.minutes > 59 || time.hours > 23 || date.date == 0 || date.date > 31 || date.weekDay > 6) {
        M5_LIB_LOGE("Invalid alarm %d:%d D:%d W:%d", time.hours, time.minutes, date.date, date.weekDay);
        return false;
    }

    // Read CONTROL2 once, then clear AF (AIE is kept, TF is written back as read)
    uint8_t ctrl2{};
    if (!read_control2(ctrl2)) {
        return false;
    }
    ctrl2 &= ~CTRL2_AF;
    ctrl2 &= CTRL2_WRITABLE;

    uint8_t buf[4]{};
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

uint32_t UnitPCF8563::writeTimer(const uint32_t msec)
{
    // Read CONTROL2 once, then clear TF
    uint8_t ctrl2{};
    if (!read_control2(ctrl2)) {
        return 0;
    }
    // Clear TF and reserved bits 7-5. AF is written back as read, TIE and TI_TP (/INT output mode) are kept
    ctrl2 &= ~CTRL2_TF;
    ctrl2 &= CTRL2_WRITABLE;

    // Round to seconds (without overflow near UINT32_MAX)
    uint32_t afterSeconds{msec / 1000 + ((msec % 1000) >= 500 ? 1U : 0U)};
    if (afterSeconds == 0) {
        if (msec) {
            M5_LIB_LOGW("Period %lu ms is rounded to 0 s, the timer is stopped", static_cast<unsigned long>(msec));
        }
        // Disable: write CONTROL2 (TF cleared, AF and TIE kept) + disable timer
        if (!write_register8(CONTROL2_REG, ctrl2) || !writeTimerControl(false, pcf8563::TimerClock::HzPM)) {
            M5_LIB_LOGW("Failed to disable timer");
        }
        return 0;
    }

    uint32_t div{1};
    pcf8563::TimerClock clock{pcf8563::TimerClock::Hz1};
    if (afterSeconds < 270) {
        if (afterSeconds > 255) {
            M5_LIB_LOGW("Period %lu ms is limited to 255 s", static_cast<unsigned long>(msec));
            afterSeconds = 255;
        }
    } else {
        div          = 60;
        afterSeconds = (afterSeconds + 30) / div;
        if (afterSeconds > 255) {
            M5_LIB_LOGW("Period %lu ms is limited to 255 min", static_cast<unsigned long>(msec));
            afterSeconds = 255;
        }
        clock = pcf8563::TimerClock::HzPM;
    }

    if (!write_register8(CONTROL2_REG, ctrl2)) {
        return 0;
    }

    // Disable timer first, then set countdown value, then enable.
    // Writing TE=1 and countdown as consecutive bytes (0x0E, 0x0F) risks the
    // timer starting with the OLD countdown value before 0x0F is updated.
    const uint8_t td{static_cast<uint8_t>(static_cast<uint8_t>(clock) & 0x03)};
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
    const uint8_t val{static_cast<uint8_t>((enabled ? 0x80 : 0x00) | (static_cast<uint8_t>(clock) & 0x03))};
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

bool UnitPCF8563::readTimerInterruptPulse(bool& pulse)
{
    pulse = false;
    uint8_t val{};
    if (!read_control2(val)) {
        return false;
    }
    pulse = (val & CTRL2_TI_TP) != 0;
    return true;
}

bool UnitPCF8563::writeTimerInterruptPulse(const bool pulse)
{
    return write_control2_bits(CTRL2_TI_TP, pulse ? CTRL2_TI_TP : 0x00);
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

    // As M5Unified, the alarm interrupt is enabled too (AF is cleared by writeAlarm() before AIE is set)
    const bool enable{at.minutes >= 0 || at.hours >= 0 || ad.date >= 0 || ad.weekDay >= 0};
    return (writeAlarm(at, ad) && writeAlarmInterrupt(enable) && enable) ? 1 : 0;
}

int UnitPCF8563::setAlarmIRQ(const int afterSeconds)
{
    if (afterSeconds < 0) {
        setTimerIRQ(0);
        return -1;
    }
    // Limit to the uint32 millisecond range
    const uint32_t sec{std::min<uint32_t>(static_cast<uint32_t>(afterSeconds), 0xFFFFFFFFUL / 1000)};
    return static_cast<int>(setTimerIRQ(sec * 1000) / 1000);
}

uint32_t UnitPCF8563::setTimerIRQ(const uint32_t timer_msec)
{
    // 0 stops the timer
    if (!timer_msec) {
        writeTimer(0);
        writeTimerInterrupt(false);
        return 0;
    }
    // As M5Unified, the timer interrupt is enabled too. TIE is set while the timer is stopped,
    // so the first countdown cannot precede it
    writeTimer(0);
    if (!writeTimerInterrupt(true)) {
        return 0;
    }
    const uint32_t actual{writeTimer(timer_msec)};
    if (!actual) {
        writeTimerInterrupt(false);
    }
    return actual;
}

void UnitPCF8563::disableIRQ()
{
    // Disable alarm (all fields disabled)
    pcf8563::rtc_time_t at{-1, -1, -1};
    pcf8563::rtc_date_t ad{2000, 1, -1, -1};
    writeAlarm(at, ad);
    // Disable timer (TD=11 for minimum power)
    writeTimerControl(false, pcf8563::TimerClock::HzPM);
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

    struct timeval tv {
    };
    tv.tv_sec = mktime(&t);
    settimeofday(&tv, tz);
}

#endif  // defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

}  // namespace unit
}  // namespace m5
