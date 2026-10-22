/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_RX8130.cpp
  @brief RX8130CE for M5UnitUnified
 */

// Include M5Unified (or at least RTC_Base) before own header so that the
// M5Unified compat API guard is satisfied when unit_RX8130.hpp is processed.
#if __has_include(<M5Unified.hpp>)
#include <M5Unified.hpp>
#elif __has_include(<utility/rtc/RTC_Base.hpp>)
#include <utility/rtc/RTC_Base.hpp>
#endif

#include "unit_RX8130.hpp"
#include <algorithm>
#include <cmath>
#include <M5Utility.hpp>
#include <driver/gpio.h>
#if __has_include(<sys/time.h>)
#include <sys/time.h>
#endif
#include <time.h>

using namespace m5::utility::mmh3;
using namespace m5::unit::types;
using namespace m5::unit::rx8130;
using namespace m5::unit::rx8130::command;

namespace {

// Extension register (1Ch)
constexpr uint8_t EXT_FSEL{0xC0};
constexpr uint8_t EXT_USEL{0x20};
constexpr uint8_t EXT_TE{0x10};
constexpr uint8_t EXT_WADA{0x08};
constexpr uint8_t EXT_TSEL{0x07};
// Flag register (1Dh), write 0 to clear (writing 1 is ignored, VBFF is read-only)
constexpr uint8_t FLAG_VBLF{0x80};
constexpr uint8_t FLAG_UF{0x20};
constexpr uint8_t FLAG_TF{0x10};
constexpr uint8_t FLAG_AF{0x08};
constexpr uint8_t FLAG_RSF{0x04};
constexpr uint8_t FLAG_VLF{0x02};
constexpr uint8_t FLAG_VBFF{0x01};
// Control register 0 (1Eh)
constexpr uint8_t CTRL0_TEST{0x80};
constexpr uint8_t CTRL0_STOP{0x40};
constexpr uint8_t CTRL0_UIE{0x20};
constexpr uint8_t CTRL0_TIE{0x10};
constexpr uint8_t CTRL0_AIE{0x08};
constexpr uint8_t CTRL0_TSTP{0x04};
constexpr uint8_t CTRL0_TBK{0x03};  // TBKON, TBKE
// Control register 1 (1Fh)
constexpr uint8_t CTRL1_SMPTSEL{0xC0};
constexpr uint8_t CTRL1_CHGEN{0x20};
constexpr uint8_t CTRL1_INIEN{0x10};
constexpr uint8_t CTRL1_RESERVED{0x08};
constexpr uint8_t CTRL1_RSVSEL{0x04};
constexpr uint8_t CTRL1_BFVSEL{0x03};
// Digital offset register (30h)
constexpr uint8_t OFFSET_DTE{0x80};
// Extension register 1 (31h)
constexpr uint8_t EXT1_VBLFE{0x01};
// Alarm registers
constexpr uint8_t ALARM_AE{0x80};

constexpr uint8_t USER_RAM_SIZE{4};

inline uint8_t bcd2byte(const uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

inline uint8_t byte2bcd(const uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

inline bool is_valid_bcd(const uint8_t bcd)
{
    return (bcd & 0x0F) <= 9 && (bcd >> 4) <= 9;
}

inline bool is_single_bit(const uint8_t v)
{
    return v && !(v & (v - 1));
}

inline int8_t bit_index(const uint8_t v)
{
    int8_t idx{};
    while (idx < 8 && !(v & (1U << idx))) {
        ++idx;
    }
    return idx;
}

// Bits that may be written as 1 (TEST and reserved bits must stay 0)
uint8_t writable_mask(const uint8_t reg)
{
    switch (reg) {
        case CONTROL0_REG:
            return static_cast<uint8_t>(~CTRL0_TEST);
        case CONTROL1_REG:
            return static_cast<uint8_t>(~CTRL1_RESERVED);
        case EXTENSION1_REG:
            return EXT1_VBLFE;
        default:
            return 0xFF;
    }
}

// Day of week (0:Sunday) by Sakamoto's method
int8_t day_of_week(const int16_t year, const int8_t month, const int8_t date)
{
    static constexpr int8_t table[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int y                     = year - (month < 3);
    return static_cast<int8_t>((y + y / 4 - y / 100 + y / 400 + table[month - 1] + date) % 7);
}

bool is_valid_time(const rtc_time_t& t)
{
    return t.hours >= 0 && t.hours <= 23 && t.minutes >= 0 && t.minutes <= 59 && t.seconds >= 0 && t.seconds <= 59;
}

bool is_valid_date(const rtc_date_t& d)
{
    return d.year >= 2000 && d.year <= 2099 && d.month >= 1 && d.month <= 12 && d.date >= 1 && d.date <= 31 &&
           d.weekDay <= 6;
}

// Wakeup timer source clocks in the order they are tried. period = mul_ms / div [ms]
struct timer_clock_t {
    uint32_t mul_ms;
    uint32_t div;
    uint32_t max_ms;   // 65535 * period, so msec * div stays within uint32
    uint16_t max_cnt;  // min(65535, 0xFFFFFFFF / mul_ms), so cnt * mul_ms stays within uint32
    TimerClock clock;
};
constexpr timer_clock_t timer_clocks[] = {
    {1000, 64, 1023984, 65535, TimerClock::Hz64},
    {1000, 1, 65535000, 65535, TimerClock::Hz1},
    {60000, 1, 3932100000UL, 65535, TimerClock::HzPM},
    {3600000, 1, 0xFFFFFFFFUL, 1193, TimerClock::HzPH},
    // Last resort: the /IRQ pulse is only 122 us with 4096 Hz (7.57 ms with the others)
    {1000, 4096, 15999, 65535, TimerClock::Hz4096},
};
// The first countdown can be short by up to one source clock (1 s for 1/60 Hz and 1/3600 Hz),
// so a minimum count bounds that error
constexpr uint32_t TIMER_MIN_COUNT{16};

// Select the finest clock (except 4096 Hz) whose count keeps the period error within 1/256
const timer_clock_t* select_timer_clock(const uint32_t msec, uint32_t& count)
{
    constexpr size_t num = sizeof(timer_clocks) / sizeof(timer_clocks[0]);
    bool overflowed{};  // a finer clock ran out of range: round up so the period never steps back
    for (size_t i = 0; i < num; ++i) {
        const auto& c = timer_clocks[i];
        if (msec > c.max_ms) {
            overflowed = true;
            continue;
        }
        const uint32_t n = msec * c.div;
        uint32_t cnt     = n / c.mul_ms;
        uint32_t err     = n % c.mul_ms;
        if (overflowed ? (err != 0) : (err * 2 >= c.mul_ms)) {
            ++cnt;
            err = c.mul_ms - err;
        }
        if (cnt > c.max_cnt) {
            cnt = c.max_cnt;
            err = n - cnt * c.mul_ms;
        }
        if (i + 1 == num || (cnt >= TIMER_MIN_COUNT && (err << 8) <= n)) {
            count = cnt;
            return &c;
        }
    }
    return nullptr;
}

}  // namespace

namespace m5 {
namespace unit {

const char UnitRX8130::name[] = "UnitRX8130";
const types::uid_t UnitRX8130::uid{"UnitRX8130"_mmh3};
const types::attr_t UnitRX8130::attr{attribute::AccessI2C};

// ISR: just set the flag (no I2C here)
void IRAM_ATTR UnitRX8130::isr_handler(void* arg)
{
    auto* self       = static_cast<UnitRX8130*>(arg);
    self->_isr_fired = true;
}

bool UnitRX8130::begin()
{
    uint8_t flags{};
    if (!read_register8(FLAG_REG, flags)) {
        M5_LIB_LOGE("Failed to read FLAG");
        return false;
    }
    if (flags & FLAG_VLF) {
        // Data may be lost (initial power-on or oscillation stop): initialize the registers
        // that keep undefined values. The time stays invalid (VLF=1) until it is written
        M5_LIB_LOGW("Oscillation stop detected. Time is invalid until set");
        const uint8_t alarm[3] = {ALARM_AE, ALARM_AE, ALARM_AE};
        const uint8_t timer[2] = {0x00, 0x00};
        // Stop the timer (TE=0) and clear WADA / TSEL before the preset is written.
        // CONTROL1: SMPTSEL / RSVSEL / BFVSEL to the power-on defaults (CHGEN / INIEN are set below)
        if (!write_register_bits(EXTENSION_REG, EXT_TE | EXT_WADA | EXT_TSEL, 0) ||
            !write_register_bits(CONTROL1_REG, CTRL1_SMPTSEL | CTRL1_RSVSEL | CTRL1_BFVSEL, 0) ||
            !write_register(ALARM_MINUTES_REG, alarm, sizeof(alarm)) ||
            !write_register(TIMER_COUNTER0_REG, timer, sizeof(timer)) || !write_register8(DIGITAL_OFFSET_REG, 0x00) ||
            !write_register8(EXTENSION1_REG, 0x00)) {
            M5_LIB_LOGE("Failed to initialize registers");
            return false;
        }
    }

    // TEST=0, STOP=0, interrupts disabled, TSTP=0, timer counts in both modes.
    // Flags are kept so that the wakeup source can be read after begin
    if (!write_register8(CONTROL0_REG, 0x00)) {
        M5_LIB_LOGE("Failed to write CONTROL0");
        return false;
    }
    // FOUT from config, USEL=0, TE=0 (WADA and TSEL are kept)
    if (!write_register_bits(EXTENSION_REG, EXT_FSEL | EXT_USEL | EXT_TE,
                             static_cast<uint8_t>(static_cast<uint8_t>(_cfg.fout) << 6))) {
        M5_LIB_LOGE("Failed to write EXTENSION");
        return false;
    }
    // Power switchover and backup battery charge
    if (_cfg.charge && !_cfg.power_switch) {
        M5_LIB_LOGW("Charging works only with the power switchover enabled");
    }
    if (!write_register_bits(CONTROL1_REG, CTRL1_CHGEN | CTRL1_INIEN,
                             (_cfg.charge ? CTRL1_CHGEN : 0) | (_cfg.power_switch ? CTRL1_INIEN : 0))) {
        M5_LIB_LOGE("Failed to write CONTROL1");
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
        if (gpio_config(&io_conf) != ESP_OK) {
            M5_LIB_LOGE("Failed to configure pin %d", _cfg.int_pin);
            return false;
        }
        auto err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // INVALID_STATE: already installed
            M5_LIB_LOGE("Failed to install ISR service %d", err);
            return false;
        }
        if (gpio_isr_handler_add(pin, isr_handler, this) != ESP_OK) {
            M5_LIB_LOGE("Failed to add ISR handler on pin %d", _cfg.int_pin);
            return false;
        }
        M5_LIB_LOGI("IRQ: hardware interrupt on pin %d", _cfg.int_pin);
    }

    _interval = _cfg.polling_interval;
    _latest   = 0;
    return true;
}

void UnitRX8130::update(const bool force)
{
    // Nothing to do without callbacks
    if (!_cfg.on_alarm && !_cfg.on_timer && !_cfg.on_update) {
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

void UnitRX8130::check_irq_flags()
{
    uint8_t flags{};
    if (!read_register8(FLAG_REG, flags)) {
        _isr_fired = !_cfg.polling;  // retry on the next update
        return;
    }
    // Handle (and clear) only the flags that have a callback; the others are left for the read functions.
    // In the hardware interrupt mode a flag with its interrupt enabled is cleared too: AF holds /IRQ low,
    // and no further falling edge (the ISR trigger) would arrive
    uint8_t handled{static_cast<uint8_t>((_cfg.on_alarm ? FLAG_AF : 0) | (_cfg.on_timer ? FLAG_TF : 0) |
                                         (_cfg.on_update ? FLAG_UF : 0))};
    if (!_cfg.polling && _cfg.int_pin >= 0) {
        uint8_t ctrl0{};
        if (!read_register8(CONTROL0_REG, ctrl0)) {
            _isr_fired = true;  // retry on the next update
            return;
        }
        handled |= ((ctrl0 & CTRL0_AIE) ? FLAG_AF : 0) | ((ctrl0 & CTRL0_TIE) ? FLAG_TF : 0) |
                   ((ctrl0 & CTRL0_UIE) ? FLAG_UF : 0);
    }
    const uint8_t fired{static_cast<uint8_t>(flags & handled)};
    if (!fired) {
        return;
    }
    // If clear fails, skip callbacks. Flags remain set and will be retried on the next check
    if (!clear_flags(fired)) {
        M5_LIB_LOGW("Failed to clear IRQ flags");
        _isr_fired = !_cfg.polling;
        return;
    }

    if ((fired & FLAG_AF) && _cfg.on_alarm) {
        _cfg.on_alarm();
    }
    if ((fired & FLAG_TF) && _cfg.on_timer) {
        _cfg.on_timer();
    }
    if ((fired & FLAG_UF) && _cfg.on_update) {
        _cfg.on_update();
    }
}

// ============================================================
// Protected helpers
// ============================================================

bool UnitRX8130::read_register(const uint8_t reg, uint8_t* buf, const size_t len)
{
    return readRegister(reg, buf, len, 0);
}

bool UnitRX8130::read_register8(const uint8_t reg, uint8_t& val)
{
    return readRegister8(reg, val, 0);
}

bool UnitRX8130::write_register(const uint8_t reg, const uint8_t* buf, const size_t len)
{
    return writeRegister(reg, buf, len);
}

bool UnitRX8130::write_register8(const uint8_t reg, const uint8_t val)
{
    return writeRegister8(reg, val & writable_mask(reg));
}

bool UnitRX8130::write_register_bits(const uint8_t reg, const uint8_t mask, const uint8_t bits)
{
    uint8_t val{};
    if (!read_register8(reg, val)) {
        return false;
    }
    return write_register8(reg, (val & ~mask) | (bits & mask));
}

bool UnitRX8130::read_bit(const uint8_t reg, const uint8_t bit, bool& on)
{
    on = false;
    uint8_t val{};
    if (!read_register8(reg, val)) {
        return false;
    }
    on = (val & bit) != 0;
    return true;
}

bool UnitRX8130::clear_flags(const uint8_t bits)
{
    // A single write leaves the other flags untouched (a read-modify-write would drop a flag raised in between)
    return write_register8(FLAG_REG, static_cast<uint8_t>(~bits));
}

bool UnitRX8130::clear_vlf()
{
    // VLF cannot be cleared until the oscillation starts (start-up time t_str is up to 1 s), so retry and verify
    constexpr uint32_t RETRY_INTERVAL_MS{10};
    constexpr int MAX_RETRY{100};
    for (int retry{}; retry < MAX_RETRY; ++retry) {
        uint8_t flags{};
        if (!clear_flags(FLAG_VLF) || !read_register8(FLAG_REG, flags)) {
            return false;
        }
        if (!(flags & FLAG_VLF)) {
            return true;
        }
        m5::utility::delay(RETRY_INTERVAL_MS);
    }
    M5_LIB_LOGE("Failed to clear VLF (oscillation not started?)");
    return false;
}

bool UnitRX8130::stop_timer()
{
    return write_register_bits(EXTENSION_REG, EXT_TE, 0) && write_register_bits(CONTROL0_REG, CTRL0_TIE, 0);
}

bool UnitRX8130::read_datetime(rx8130::rtc_date_t* date, rx8130::rtc_time_t* time)
{
    const uint8_t start_reg = (time != nullptr) ? SECONDS_REG : WEEK_REG;
    const size_t len        = ((date != nullptr) ? 4 : 0) + ((time != nullptr) ? 3 : 0);
    if (len == 0) {
        return false;
    }

    // Read in one transaction: the chip holds the carry during the access
    uint8_t buf[7]{};
    if (!read_register(start_reg, buf, len)) {
        M5_LIB_LOGE("Failed to read datetime");
        return false;
    }

    size_t idx = 0;
    rx8130::rtc_time_t t{};
    if (time) {
        const uint8_t sec  = buf[idx++] & 0x7F;
        const uint8_t min  = buf[idx++] & 0x7F;
        const uint8_t hour = buf[idx++] & 0x3F;
        if (!is_valid_bcd(sec) || !is_valid_bcd(min) || !is_valid_bcd(hour)) {
            M5_LIB_LOGE("Invalid time %02X:%02X:%02X", hour, min, sec);
            return false;
        }
        t.seconds = bcd2byte(sec);
        t.minutes = bcd2byte(min);
        t.hours   = bcd2byte(hour);
    }
    rx8130::rtc_date_t d{};
    if (date) {
        const uint8_t week  = buf[idx++] & 0x7F;
        const uint8_t day   = buf[idx++] & 0x3F;
        const uint8_t month = buf[idx++] & 0x1F;
        const uint8_t year  = buf[idx];
        if (!is_single_bit(week) || !is_valid_bcd(day) || !is_valid_bcd(month) || !is_valid_bcd(year)) {
            M5_LIB_LOGE("Invalid date %02X-%02X-%02X W:%02X", year, month, day, week);
            return false;
        }
        d.weekDay = bit_index(week);
        d.date    = bcd2byte(day);
        d.month   = bcd2byte(month);
        d.year    = 2000 + bcd2byte(year);
    }
    if ((time && !is_valid_time(t)) || (date && !is_valid_date(d))) {
        M5_LIB_LOGE("Out of range datetime");
        return false;
    }
    if (time) {
        *time = t;
    }
    if (date) {
        *date = d;
    }
    return true;
}

bool UnitRX8130::write_datetime(const rx8130::rtc_date_t* date, const rx8130::rtc_time_t* time)
{
    uint8_t buf[7]{};
    size_t idx        = 0;
    uint8_t reg_start = WEEK_REG;

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
        const int8_t wd = (date->weekDay < 0) ? day_of_week(date->year, date->month, date->date) : date->weekDay;
        buf[idx++]      = static_cast<uint8_t>(1U << wd);
        buf[idx++]      = byte2bcd(date->date);
        buf[idx++]      = byte2bcd(date->month);
        buf[idx++]      = byte2bcd(date->year % 100);
    }
    if (idx == 0) {
        return false;
    }
    if (!write_register(reg_start, buf, idx)) {
        M5_LIB_LOGE("Failed to write datetime");
        return false;
    }
    // The date and time are valid again only when both are written
    return (time && date) ? clear_vlf() : true;
}

bool UnitRX8130::write_alarm(const uint8_t min, const uint8_t hour, const uint8_t day_week, const bool wada,
                             const bool enable)
{
    // Sequence per datasheet (Figure 49): AIE=0 -> alarm registers -> WADA -> clear AF -> AIE=1,
    // so a flag raised while the alarm is changed is cleared too
    if (!write_register_bits(CONTROL0_REG, CTRL0_AIE, 0)) {
        return false;
    }
    const uint8_t buf[3] = {min, hour, day_week};
    if (!write_register(ALARM_MINUTES_REG, buf, sizeof(buf)) ||
        !write_register_bits(EXTENSION_REG, EXT_WADA, wada ? EXT_WADA : 0) || !clear_flags(FLAG_AF)) {
        M5_LIB_LOGE("Failed to write alarm registers");
        return false;
    }
    // All AE=1 would raise the alarm every minute, so the interrupt stays disabled without any field
    return enable ? write_register_bits(CONTROL0_REG, CTRL0_AIE, CTRL0_AIE) : true;
}

// ============================================================
// M5UnitUnified style API
// ============================================================

// ---- Date/Time ----

bool UnitRX8130::readTime(rx8130::rtc_time_t& time)
{
    time = {};
    return read_datetime(nullptr, &time);
}

bool UnitRX8130::writeTime(const rx8130::rtc_time_t& time)
{
    return write_datetime(nullptr, &time);
}

bool UnitRX8130::readDate(rx8130::rtc_date_t& date)
{
    date = {};
    return read_datetime(&date, nullptr);
}

bool UnitRX8130::writeDate(const rx8130::rtc_date_t& date)
{
    return write_datetime(&date, nullptr);
}

bool UnitRX8130::readDateTime(rx8130::rtc_datetime_t& dt)
{
    dt = rx8130::rtc_datetime_t{};
    return read_datetime(&dt.date, &dt.time);
}

bool UnitRX8130::writeDateTime(const rx8130::rtc_datetime_t& dt)
{
    return write_datetime(&dt.date, &dt.time);
}

bool UnitRX8130::readDateTime(struct tm& t)
{
    t = {};
    rx8130::rtc_datetime_t dt{};
    if (!readDateTime(dt)) {
        return false;
    }
    t = dt.to_tm();
    return true;
}

bool UnitRX8130::writeDateTime(const struct tm& t)
{
    return writeDateTime(rx8130::rtc_datetime_t(t));
}

// ---- Alarm ----

bool UnitRX8130::writeAlarm(const rx8130::rtc_time_t& time, const rx8130::rtc_date_t& date)
{
    if (time.minutes > 59 || time.hours > 23 || date.date == 0 || date.date > 31 || date.weekDay > 6) {
        M5_LIB_LOGE("Invalid alarm %d:%d D:%d W:%d", time.hours, time.minutes, date.date, date.weekDay);
        return false;
    }
    const uint8_t min  = (time.minutes < 0) ? ALARM_AE : byte2bcd(time.minutes);
    const uint8_t hour = (time.hours < 0) ? ALARM_AE : byte2bcd(time.hours);
    uint8_t day_week{ALARM_AE};
    bool wada{true};
    if (date.date >= 0) {
        day_week = byte2bcd(date.date);
    } else if (date.weekDay >= 0) {
        day_week = static_cast<uint8_t>(1U << date.weekDay);
        wada     = false;
    }
    const bool enable = (time.minutes >= 0 || time.hours >= 0 || date.date >= 0 || date.weekDay >= 0);
    return write_alarm(min, hour, day_week, wada, enable);
}

bool UnitRX8130::writeAlarmWeekdays(const rx8130::rtc_time_t& time, const uint8_t weekdays)
{
    if (time.minutes > 59 || time.hours > 23 || weekdays > 0x7F) {
        M5_LIB_LOGE("Invalid alarm %d:%d W:%02X", time.hours, time.minutes, weekdays);
        return false;
    }
    const uint8_t min  = (time.minutes < 0) ? ALARM_AE : byte2bcd(time.minutes);
    const uint8_t hour = (time.hours < 0) ? ALARM_AE : byte2bcd(time.hours);
    const bool enable  = (time.minutes >= 0 || time.hours >= 0 || weekdays);
    return write_alarm(min, hour, weekdays ? weekdays : ALARM_AE, false, enable);
}

bool UnitRX8130::readAlarm(rx8130::rtc_time_t& time, rx8130::rtc_date_t& date)
{
    time = {};
    date = {};
    uint8_t buf[3]{};
    uint8_t ext{};
    if (!read_register(ALARM_MINUTES_REG, buf, sizeof(buf)) || !read_register8(EXTENSION_REG, ext)) {
        M5_LIB_LOGE("Failed to read alarm registers");
        return false;
    }
    time.minutes = (buf[0] & ALARM_AE) ? -1 : static_cast<int8_t>(bcd2byte(buf[0] & 0x7F));
    time.hours   = (buf[1] & ALARM_AE) ? -1 : static_cast<int8_t>(bcd2byte(buf[1] & 0x3F));
    time.seconds = -1;  // Seconds not part of alarm
    date.date    = -1;
    date.weekDay = -1;
    if (!(buf[2] & ALARM_AE)) {
        if (ext & EXT_WADA) {
            date.date = static_cast<int8_t>(bcd2byte(buf[2] & 0x3F));
        } else if (is_single_bit(buf[2] & 0x7F)) {
            date.weekDay = bit_index(buf[2] & 0x7F);
        }
    }
    // Year and month not part of alarm
    date.year  = 2000;
    date.month = 1;
    return true;
}

bool UnitRX8130::readAlarmWeekdays(uint8_t& weekdays)
{
    weekdays = 0;
    uint8_t dw{};
    uint8_t ext{};
    if (!read_register8(ALARM_WEEK_DAY_REG, dw) || !read_register8(EXTENSION_REG, ext)) {
        return false;
    }
    if (!(dw & ALARM_AE) && !(ext & EXT_WADA)) {
        weekdays = dw & 0x7F;
    }
    return true;
}

bool UnitRX8130::readAlarmInterrupt(bool& enabled)
{
    return read_bit(CONTROL0_REG, CTRL0_AIE, enabled);
}

bool UnitRX8130::writeAlarmInterrupt(const bool enabled)
{
    return write_register_bits(CONTROL0_REG, CTRL0_AIE, enabled ? CTRL0_AIE : 0);
}

bool UnitRX8130::readAlarmFlag(bool& fired)
{
    return read_bit(FLAG_REG, FLAG_AF, fired);
}

bool UnitRX8130::clearAlarmFlag()
{
    return clear_flags(FLAG_AF);
}

// ---- Wakeup timer ----

uint32_t UnitRX8130::writeTimer(const uint32_t msec)
{
    if (msec == 0) {
        if (!stop_timer() || !clear_flags(FLAG_TF)) {
            M5_LIB_LOGW("Failed to disable timer");
        }
        return 0;
    }

    uint32_t count{};
    const auto* sel = select_timer_clock(msec, count);
    if (!sel) {
        return 0;
    }

    // Sequence per datasheet: TE=0 (+TSEL) -> clear TF -> TIE -> preset -> TE=1 last,
    // so the first event cannot precede TIE
    const uint8_t tsel     = static_cast<uint8_t>(sel->clock);
    const uint8_t preset[] = {static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>(count >> 8)};
    bool ok                = write_register_bits(EXTENSION_REG, EXT_TE | EXT_TSEL, tsel) && clear_flags(FLAG_TF) &&
              write_register_bits(CONTROL0_REG, CTRL0_TIE, CTRL0_TIE);
    if (ok) {
        // While TE=0 the counter reads back the preset, so verify the write
        ok = false;
        for (int retry = 0; retry < 3 && !ok; ++retry) {
            uint8_t verify[2]{};
            ok = write_register(TIMER_COUNTER0_REG, preset, sizeof(preset)) &&
                 read_register(TIMER_COUNTER0_REG, verify, sizeof(verify)) && verify[0] == preset[0] &&
                 verify[1] == preset[1];
        }
    }
    if (!ok || !write_register_bits(EXTENSION_REG, EXT_TE, EXT_TE)) {
        // Timer may be in inconsistent state; disable it
        stop_timer();
        return 0;
    }

    // Actual period rounded to the nearest ms (never 0 while running)
    const uint32_t result = (count * sel->mul_ms + (sel->div >> 1)) / sel->div;
    return result ? result : 1;
}

bool UnitRX8130::readTimerControl(bool& enabled, rx8130::TimerClock& clock)
{
    enabled = false;
    clock   = rx8130::TimerClock::Hz4096;
    uint8_t val{};
    if (!read_register8(EXTENSION_REG, val)) {
        return false;
    }
    enabled = (val & EXT_TE) != 0;
    clock   = static_cast<rx8130::TimerClock>(val & EXT_TSEL);
    return true;
}

bool UnitRX8130::writeTimerControl(const bool enabled, const rx8130::TimerClock clock)
{
    // Change TSEL with TE=0, then start
    if (!write_register_bits(EXTENSION_REG, EXT_TE | EXT_TSEL, static_cast<uint8_t>(clock) & EXT_TSEL)) {
        return false;
    }
    if (!enabled) {
        return true;
    }
    return write_register_bits(EXTENSION_REG, EXT_TE, EXT_TE);
}

bool UnitRX8130::readTimerValue(uint16_t& count)
{
    count = 0;
    // The counter is not held during the access, so read until two values match
    uint8_t prev[2]{};
    if (!read_register(TIMER_COUNTER0_REG, prev, sizeof(prev))) {
        return false;
    }
    for (int retry = 0; retry < 3; ++retry) {
        uint8_t cur[2]{};
        if (!read_register(TIMER_COUNTER0_REG, cur, sizeof(cur))) {
            return false;
        }
        if (cur[0] == prev[0] && cur[1] == prev[1]) {
            count = m5::types::little_uint16_t{cur[0], cur[1]}.get();
            return true;
        }
        prev[0] = cur[0];
        prev[1] = cur[1];
    }
    return false;
}

bool UnitRX8130::writeTimerValue(const uint16_t count)
{
    bool running{};
    if (count == 0 || !read_bit(EXTENSION_REG, EXT_TE, running)) {
        return false;
    }
    if (running) {
        M5_LIB_LOGE("Timer is running (TE=1)");
        return false;
    }
    const uint8_t buf[2] = {static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>(count >> 8)};
    return write_register(TIMER_COUNTER0_REG, buf, sizeof(buf));
}

bool UnitRX8130::readTimerInterrupt(bool& enabled)
{
    return read_bit(CONTROL0_REG, CTRL0_TIE, enabled);
}

bool UnitRX8130::writeTimerInterrupt(const bool enabled)
{
    return write_register_bits(CONTROL0_REG, CTRL0_TIE, enabled ? CTRL0_TIE : 0);
}

bool UnitRX8130::readTimerFlag(bool& fired)
{
    return read_bit(FLAG_REG, FLAG_TF, fired);
}

bool UnitRX8130::clearTimerFlag()
{
    return clear_flags(FLAG_TF);
}

bool UnitRX8130::readTimerPause(bool& paused)
{
    return read_bit(CONTROL0_REG, CTRL0_TSTP, paused);
}

bool UnitRX8130::writeTimerPause(const bool pause)
{
    return write_register_bits(CONTROL0_REG, CTRL0_TSTP, pause ? CTRL0_TSTP : 0);
}

bool UnitRX8130::readTimerCountMode(rx8130::TimerCountMode& mode)
{
    mode = rx8130::TimerCountMode::Always;
    uint8_t val{};
    if (!read_register8(CONTROL0_REG, val)) {
        return false;
    }
    // TBKON is ignored while TBKE=0
    mode = (val & 0x01) ? static_cast<rx8130::TimerCountMode>(val & CTRL0_TBK) : rx8130::TimerCountMode::Always;
    return true;
}

bool UnitRX8130::writeTimerCountMode(const rx8130::TimerCountMode mode)
{
    return write_register_bits(CONTROL0_REG, CTRL0_TBK, static_cast<uint8_t>(mode));
}

// ---- Time update interrupt ----

bool UnitRX8130::readUpdateTiming(rx8130::UpdateTiming& timing)
{
    bool minute{};
    if (!read_bit(EXTENSION_REG, EXT_USEL, minute)) {
        timing = rx8130::UpdateTiming::Second;
        return false;
    }
    timing = minute ? rx8130::UpdateTiming::Minute : rx8130::UpdateTiming::Second;
    return true;
}

bool UnitRX8130::writeUpdateTiming(const rx8130::UpdateTiming timing)
{
    return write_register_bits(EXTENSION_REG, EXT_USEL, (timing == rx8130::UpdateTiming::Minute) ? EXT_USEL : 0);
}

bool UnitRX8130::readUpdateInterrupt(bool& enabled)
{
    return read_bit(CONTROL0_REG, CTRL0_UIE, enabled);
}

bool UnitRX8130::writeUpdateInterrupt(const bool enabled)
{
    return write_register_bits(CONTROL0_REG, CTRL0_UIE, enabled ? CTRL0_UIE : 0);
}

bool UnitRX8130::readUpdateFlag(bool& fired)
{
    return read_bit(FLAG_REG, FLAG_UF, fired);
}

bool UnitRX8130::clearUpdateFlag()
{
    return clear_flags(FLAG_UF);
}

// ---- Clock control ----

bool UnitRX8130::readStop(bool& stopped)
{
    return read_bit(CONTROL0_REG, CTRL0_STOP, stopped);
}

bool UnitRX8130::writeStop(const bool stop)
{
    return write_register_bits(CONTROL0_REG, CTRL0_STOP, stop ? CTRL0_STOP : 0);
}

bool UnitRX8130::readClockOutput(rx8130::ClockOutput& fout)
{
    fout = rx8130::ClockOutput::None;
    uint8_t val{};
    if (!read_register8(EXTENSION_REG, val)) {
        return false;
    }
    fout = static_cast<rx8130::ClockOutput>(val >> 6);
    return true;
}

bool UnitRX8130::writeClockOutput(const rx8130::ClockOutput fout)
{
    return write_register_bits(EXTENSION_REG, EXT_FSEL, static_cast<uint8_t>(static_cast<uint8_t>(fout) << 6));
}

bool UnitRX8130::readDigitalOffset(bool& enabled, int8_t& steps)
{
    enabled = false;
    steps   = 0;
    uint8_t val{};
    if (!read_register8(DIGITAL_OFFSET_REG, val)) {
        return false;
    }
    enabled = (val & OFFSET_DTE) != 0;
    steps   = m5::utility::unsigned_to_signed<7>(static_cast<uint8_t>(val & 0x7F));  // 7-bit two's complement
    return true;
}

bool UnitRX8130::writeDigitalOffset(const bool enabled, const int8_t steps)
{
    if (steps < DIGITAL_OFFSET_MIN || steps > DIGITAL_OFFSET_MAX) {
        M5_LIB_LOGE("Invalid offset %d", steps);
        return false;
    }
    return write_register8(DIGITAL_OFFSET_REG, (enabled ? OFFSET_DTE : 0) | (static_cast<uint8_t>(steps) & 0x7F));
}

bool UnitRX8130::writeDigitalOffsetPPM(const float ppm)
{
    if (ppm < -195.31f || ppm > 192.26f) {
        M5_LIB_LOGE("Invalid offset %f ppm", ppm);
        return false;
    }
    if (ppm == 0.0f) {
        return writeDigitalOffset(false, 0);
    }
    // Round |ppm| / 3.05 to the nearest step. The text of the datasheet says "decimals are discarded",
    // but all its examples are rounded: +192 -> 63, -158 -> L=76 (-52), +11.57 -> 4, -11.57 -> L=124 (-4)
    const long x = std::lround((ppm < 0 ? -ppm : ppm) / DIGITAL_OFFSET_STEP_PPM);
    int steps    = static_cast<int>(ppm > 0 ? x : -x);
    steps        = std::max<int>(DIGITAL_OFFSET_MIN, std::min<int>(DIGITAL_OFFSET_MAX, steps));
    return writeDigitalOffset(true, static_cast<int8_t>(steps));
}

// ---- Status ----

bool UnitRX8130::readVoltLow(bool& low)
{
    return read_bit(FLAG_REG, FLAG_VLF, low);
}

bool UnitRX8130::clearVoltLow()
{
    return clear_flags(FLAG_VLF);
}

bool UnitRX8130::readBatteryLow(bool& low)
{
    return read_bit(FLAG_REG, FLAG_VBLF, low);
}

bool UnitRX8130::clearBatteryLow()
{
    return clear_flags(FLAG_VBLF);
}

bool UnitRX8130::readBatteryFull(bool& full)
{
    return read_bit(FLAG_REG, FLAG_VBFF, full);
}

bool UnitRX8130::readResetFlag(bool& detected)
{
    return read_bit(FLAG_REG, FLAG_RSF, detected);
}

bool UnitRX8130::clearResetFlag()
{
    return clear_flags(FLAG_RSF);
}

// ---- Backup power supply ----

bool UnitRX8130::readPowerSwitch(bool& enabled)
{
    return read_bit(CONTROL1_REG, CTRL1_INIEN, enabled);
}

bool UnitRX8130::writePowerSwitch(const bool enabled)
{
    return write_register_bits(CONTROL1_REG, CTRL1_INIEN, enabled ? CTRL1_INIEN : 0);
}

bool UnitRX8130::readBatteryCharge(bool& enabled)
{
    return read_bit(CONTROL1_REG, CTRL1_CHGEN, enabled);
}

bool UnitRX8130::writeBatteryCharge(const bool enabled)
{
    return write_register_bits(CONTROL1_REG, CTRL1_CHGEN, enabled ? CTRL1_CHGEN : 0);
}

bool UnitRX8130::readFullChargeVoltage(rx8130::FullChargeVoltage& voltage)
{
    voltage = rx8130::FullChargeVoltage::V3_02;
    uint8_t val{};
    if (!read_register8(CONTROL1_REG, val)) {
        return false;
    }
    voltage = static_cast<rx8130::FullChargeVoltage>(val & CTRL1_BFVSEL);
    return true;
}

bool UnitRX8130::writeFullChargeVoltage(const rx8130::FullChargeVoltage voltage)
{
    return write_register_bits(CONTROL1_REG, CTRL1_BFVSEL, static_cast<uint8_t>(voltage));
}

bool UnitRX8130::readResetVoltage(rx8130::ResetVoltage& voltage)
{
    bool v2_70{};
    if (!read_bit(CONTROL1_REG, CTRL1_RSVSEL, v2_70)) {
        voltage = rx8130::ResetVoltage::V2_75;
        return false;
    }
    voltage = v2_70 ? rx8130::ResetVoltage::V2_70 : rx8130::ResetVoltage::V2_75;
    return true;
}

bool UnitRX8130::writeResetVoltage(const rx8130::ResetVoltage voltage)
{
    return write_register_bits(CONTROL1_REG, CTRL1_RSVSEL, (voltage == rx8130::ResetVoltage::V2_70) ? CTRL1_RSVSEL : 0);
}

bool UnitRX8130::readDetectionTime(rx8130::DetectionTime& time)
{
    time = rx8130::DetectionTime::Ms2;
    uint8_t val{};
    if (!read_register8(CONTROL1_REG, val)) {
        return false;
    }
    time = static_cast<rx8130::DetectionTime>(val >> 6);
    return true;
}

bool UnitRX8130::writeDetectionTime(const rx8130::DetectionTime time)
{
    return write_register_bits(CONTROL1_REG, CTRL1_SMPTSEL, static_cast<uint8_t>(static_cast<uint8_t>(time) << 6));
}

bool UnitRX8130::readBatteryLowDetection(bool& enabled)
{
    return read_bit(EXTENSION1_REG, EXT1_VBLFE, enabled);
}

bool UnitRX8130::writeBatteryLowDetection(const bool enabled)
{
    return write_register8(EXTENSION1_REG, enabled ? EXT1_VBLFE : 0);
}

// ---- User RAM ----

bool UnitRX8130::readUserRAM(uint8_t* buf, const uint8_t offset, const uint8_t len)
{
    if (!buf || !len || offset + len > USER_RAM_SIZE) {
        return false;
    }
    return read_register(USER_RAM_REG + offset, buf, len);
}

bool UnitRX8130::writeUserRAM(const uint8_t* buf, const uint8_t offset, const uint8_t len)
{
    if (!buf || !len || offset + len > USER_RAM_SIZE) {
        return false;
    }
    return write_register(USER_RAM_REG + offset, buf, len);
}

// ============================================================
// M5Unified compatible API
// ============================================================

#if defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

int UnitRX8130::setAlarmIRQ(const m5::rtc_date_t* date, const m5::rtc_time_t* time)
{
    rx8130::rtc_time_t at{-1, -1, -1};
    rx8130::rtc_date_t ad{2000, 1, -1, -1};

    if (time) {
        at.minutes = time->minutes < 0 ? -1 : time->minutes;
        at.hours   = time->hours < 0 ? -1 : time->hours;
    }
    if (date) {
        if (date->date >= 0) {
            ad.date = date->date;
        } else if (date->weekDay >= 0) {
            ad.weekDay = date->weekDay;
        }
    }
    const bool enable = (at.minutes >= 0 || at.hours >= 0 || ad.date >= 0 || ad.weekDay >= 0);
    return (writeAlarm(at, ad) && enable) ? 1 : 0;
}

int UnitRX8130::setAlarmIRQ(const int afterSeconds)
{
    if (afterSeconds < 0) {
        setTimerIRQ(0);
        return -1;
    }
    // Limit to the uint32 millisecond range
    const uint32_t sec = std::min<uint32_t>(static_cast<uint32_t>(afterSeconds), 0xFFFFFFFFUL / 1000);
    return static_cast<int>(setTimerIRQ(sec * 1000) / 1000);
}

uint32_t UnitRX8130::setTimerIRQ(const uint32_t timer_msec)
{
    return writeTimer(timer_msec);
}

bool UnitRX8130::getIRQstatus()
{
    uint8_t flags{};
    if (!read_register8(FLAG_REG, flags)) {
        return false;
    }
    return (flags & (FLAG_TF | FLAG_AF)) != 0;
}

void UnitRX8130::clearIRQ()
{
    clear_flags(FLAG_TF | FLAG_AF);
}

void UnitRX8130::disableIRQ()
{
    write_register_bits(CONTROL0_REG, CTRL0_AIE | CTRL0_TIE | CTRL0_UIE, 0);
    write_register_bits(EXTENSION_REG, EXT_TE, 0);
    clear_flags(FLAG_TF | FLAG_AF | FLAG_UF);
}

void UnitRX8130::setSystemTimeFromRtc(struct timezone* tz)
{
    rx8130::rtc_datetime_t dt;
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
