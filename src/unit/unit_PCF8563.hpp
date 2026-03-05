/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_PCF8563.hpp
  @brief PCF8563 Unit for M5UnitUnified
 */
#ifndef M5_UNIT_RTC_UNIT_PCF8563_HPP
#define M5_UNIT_RTC_UNIT_PCF8563_HPP

#include <M5UnitComponent.hpp>
#include "unit_PCF8563_types.hpp"

namespace m5 {
namespace unit {

/*!
  @class UnitPCF8563
  @brief Real-time clock unit using PCF8563 compatible chip (BM8563/HYM8563)
 */
class UnitPCF8563 : public Component {
    M5_UNIT_COMPONENT_HPP_BUILDER(UnitPCF8563, 0x51);

public:
    //! @brief IRQ callback type
    using irq_callback_t = void (*)();

    /*!
      @struct config_t
      @brief Settings for begin
     */
    struct config_t {
        //! GPIO pin connected to PCF8563 INT pin (-1: not connected)
        int16_t int_pin{-1};
        //! true: poll IRQ status via I2C in update(), false: use hardware interrupt via int_pin
        bool polling{true};
        //! CLKOUT pin output frequency (default: disabled)
        pcf8563::ClockOutput clkout{pcf8563::ClockOutput::None};
        //! Polling interval in milliseconds for IRQ status check in update()
        uint32_t polling_interval{500};
        //! Callback invoked from update() when alarm fires (nullptr: disabled)
        irq_callback_t on_alarm{nullptr};
        //! Callback invoked from update() when timer fires (nullptr: disabled)
        irq_callback_t on_timer{nullptr};
    };

    explicit UnitPCF8563(const uint8_t addr = DEFAULT_ADDRESS) : Component(addr)
    {
    }
    virtual ~UnitPCF8563()
    {
    }

    virtual bool begin() override;
    virtual void update(const bool force = false) override;

    ///@name Settings for begin
    ///@{
    /*! @brief Gets the configuration */
    inline config_t config() const
    {
        return _cfg;
    }
    //! @brief Set the configuration
    inline void config(const config_t& cfg)
    {
        _cfg = cfg;
    }
    ///@}

    // ============================================================
    // M5UnitUnified style API

    ///@name Date/Time
    ///@{
    /*!
      @brief Read time from RTC
      @param[out] time Time (hours, minutes, seconds)
      @return True if successful
     */
    bool readTime(pcf8563::rtc_time_t& time);
    /*!
      @brief Write time to RTC
      @param time Time (hours, minutes, seconds)
      @return True if successful
     */
    bool writeTime(const pcf8563::rtc_time_t& time);
    /*!
      @brief Read date from RTC
      @param[out] date Date (year, month, date, weekDay)
      @return True if successful
     */
    bool readDate(pcf8563::rtc_date_t& date);
    /*!
      @brief Write date to RTC
      @param date Date (year, month, date, weekDay)
      @return True if successful
     */
    bool writeDate(const pcf8563::rtc_date_t& date);
    /*!
      @brief Read date and time from RTC
      @param[out] dt DateTime
      @return True if successful
     */
    bool readDateTime(pcf8563::rtc_datetime_t& dt);
    /*!
      @brief Write date and time to RTC
      @param dt DateTime
      @return True if successful
     */
    bool writeDateTime(const pcf8563::rtc_datetime_t& dt);
    /*!
      @brief Read date and time from RTC into struct tm
      @param[out] t struct tm (date and time fields are set; tm_isdst = -1)
      @return True if successful
     */
    bool readDateTime(struct tm& t);
    /*!
      @brief Write date and time to RTC from struct tm
      @param t struct tm
      @return True if successful
     */
    bool writeDateTime(const struct tm& t);
    ///@}

    /*!
      @note About IRQ/Alarm/Timer:
      These functions configure the PCF8563 chip's internal alarm/timer registers
      and interrupt flags via I2C. When triggered, the chip drives its INT pin
      (open-drain, active LOW).

      @note The INT pin is **not** wired on standard GROVE/Port A/B/C connectors
      (4-pin: GND, VCC, SDA, SCL only).

      @note Two modes are supported via config_t:
      - **Polling mode** (default, `polling=true`): update() periodically reads
        the IRQ status register via I2C (every 500ms) and invokes the callback.
        Works without the INT pin (GROVE compatible).
      - **Hardware interrupt mode** (`polling=false`, `int_pin` set): begin()
        calls attachInterrupt() on the specified GPIO. The ISR sets an internal
        flag; update() detects it and invokes the callback. Requires a direct
        connection from the PCF8563 INT pin to a host GPIO.

      @note In both modes, callbacks (on_alarm / on_timer) are called from update(),
      never from an ISR context. Set them in config_t before calling begin().
     */
    ///@name Alarm/Timer
    ///@{
    /*!
      @brief Set alarm and enable interrupt
      @param time Alarm time (minutes, hours). Negative fields are disabled.
      @param date Alarm date (date, weekDay). Negative fields are disabled.
      @return True if successful
      @note Clears alarm flag, writes alarm registers, and enables alarm interrupt
            if any field is non-negative. Disables alarm interrupt if all fields are negative.
     */
    bool writeAlarm(const pcf8563::rtc_time_t& time, const pcf8563::rtc_date_t& date);
    /*!
      @brief Read alarm settings
      @param[out] time Alarm time (minutes, hours; seconds always -1). Disabled fields are -1.
      @param[out] date Alarm date (date, weekDay; year/month unused). Disabled fields are -1.
      @return True if successful
     */
    bool readAlarm(pcf8563::rtc_time_t& time, pcf8563::rtc_date_t& date);
    /*!
      @brief Read alarm interrupt enable state
      @param[out] enabled True if alarm interrupt is enabled (AIE bit)
      @return True if successful
     */
    bool readAlarmInterrupt(bool& enabled);
    /*!
      @brief Write alarm interrupt enable state
      @param enabled True to enable alarm interrupt (AIE bit)
      @return True if successful
     */
    bool writeAlarmInterrupt(const bool enabled);
    /*!
      @brief Read alarm flag
      @param[out] fired True if alarm has fired (AF bit)
      @return True if successful
     */
    bool readAlarmFlag(bool& fired);
    /*!
      @brief Clear alarm flag (AF bit)
      @return True if successful
     */
    bool clearAlarmFlag();

    /*!
      @brief Set countdown timer with millisecond duration
      @param msec Duration in milliseconds (0 to disable)
      @param repeat True for periodic (auto-reload via TI_TP), false for one-shot
      @return Actual duration in milliseconds (0 if disabled)
      @note Automatically selects Hz1 (< 270s) or HzPM (>= 270s) clock source.
            Clears timer/alarm flags, enables timer and timer interrupt.
            Maximum: 255 minutes (15,300,000 ms).
     */
    uint32_t writeTimer(const uint32_t msec, const bool repeat = false);
    /*!
      @brief Read timer control settings
      @param[out] enabled True if timer is enabled (TE bit)
      @param[out] clock Timer clock source (TD bits)
      @return True if successful
     */
    bool readTimerControl(bool& enabled, pcf8563::TimerClock& clock);
    /*!
      @brief Write timer control settings
      @param enabled True to enable timer (TE bit)
      @param clock Timer clock source (TD bits)
      @return True if successful
     */
    bool writeTimerControl(const bool enabled, const pcf8563::TimerClock clock);
    /*!
      @brief Read timer countdown value
      @param[out] count Countdown value (0-255)
      @return True if successful
     */
    bool readTimerValue(uint8_t& count);
    /*!
      @brief Write timer countdown value
      @param count Countdown value (0-255)
      @return True if successful
     */
    bool writeTimerValue(const uint8_t count);
    /*!
      @brief Read timer interrupt enable state
      @param[out] enabled True if timer interrupt is enabled (TIE bit)
      @return True if successful
     */
    bool readTimerInterrupt(bool& enabled);
    /*!
      @brief Write timer interrupt enable state
      @param enabled True to enable timer interrupt (TIE bit)
      @return True if successful
     */
    bool writeTimerInterrupt(const bool enabled);
    /*!
      @brief Read timer flag
      @param[out] fired True if timer has fired (TF bit)
      @return True if successful
     */
    bool readTimerFlag(bool& fired);
    /*!
      @brief Clear timer flag (TF bit)
      @return True if successful
     */
    bool clearTimerFlag();
    /*!
      @brief Read timer periodic mode (TI_TP bit)
      @param[out] periodic True if periodic (auto-reload), false if one-shot
      @return True if successful
      @note When periodic, the countdown timer automatically restarts from the
            preset value each time it reaches zero. TF flag is set each time.
     */
    bool readTimerPeriodic(bool& periodic);
    /*!
      @brief Write timer periodic mode (TI_TP bit)
      @param periodic True for periodic (auto-reload), false for one-shot
      @return True if successful
      @note When periodic, the countdown timer automatically restarts from the
            preset value each time it reaches zero. TF flag is set each time.
     */
    bool writeTimerPeriodic(const bool periodic);
    ///@}

    ///@name Clock Control
    ///@{
    /*!
      @brief Read RTC clock stop state
      @param[out] stopped True if RTC clock is stopped (STOP bit)
      @return True if successful
     */
    bool readStop(bool& stopped);
    /*!
      @brief Write RTC clock stop state
      @param stop True to stop RTC clock, false to start
      @return True if successful
      @note While stopped, the prescaler (F2-F14) is held in reset and no
            1 Hz ticks are generated. The first increment after restart occurs
            approximately 0.508 seconds later. CLKOUT 32.768 kHz is unaffected.
     */
    bool writeStop(const bool stop);
    ///@}

    ///@name Status
    ///@{
    /*!
      @brief Read voltage-low flag (battery backup status)
      @param[out] low True if voltage is low (VL bit in seconds register)
      @return True if successful
     */
    bool readVoltLow(bool& low);
    ///@}

    // ============================================================
    // M5Unified M5.RTC compatible API
    // Available when M5Unified.hpp or RTC_Base.hpp is included before this header.
#if defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

    ///@name M5Unified M5.RTC compatible API
    ///@{

    /*!
      @brief Get date and/or time
      @param[out] date Date (can be nullptr)
      @param[out] time Time (can be nullptr)
      @return True if successful
     */
    inline bool getDateTime(m5::rtc_date_t* date, m5::rtc_time_t* time)
    {
        pcf8563::rtc_date_t d;
        pcf8563::rtc_time_t t;
        if (!read_datetime(date ? &d : nullptr, time ? &t : nullptr)) {
            return false;
        }
        if (date) {
            *date = {d.year, d.month, d.date, d.weekDay};
        }
        if (time) {
            *time = {t.hours, t.minutes, t.seconds};
        }
        return true;
    }
    /*!
      @brief Set date and/or time
      @param date Date (can be nullptr to skip)
      @param time Time (can be nullptr to skip)
      @return True if successful
     */
    inline bool setDateTime(const m5::rtc_date_t* date, const m5::rtc_time_t* time)
    {
        pcf8563::rtc_date_t d;
        pcf8563::rtc_time_t t;
        if (date) {
            d = {date->year, date->month, date->date, date->weekDay};
        }
        if (time) {
            t = {time->hours, time->minutes, time->seconds};
        }
        return write_datetime(date ? &d : nullptr, time ? &t : nullptr);
    }

    //! @brief Get datetime
    inline bool getDateTime(m5::rtc_datetime_t* dt)
    {
        return dt ? getDateTime(&dt->date, &dt->time) : false;
    }
    //! @brief Get date only
    inline bool getDate(m5::rtc_date_t* date)
    {
        return getDateTime(date, nullptr);
    }
    //! @brief Get time only
    inline bool getTime(m5::rtc_time_t* time)
    {
        return getDateTime(nullptr, time);
    }

    //! @brief Set datetime
    inline bool setDateTime(const m5::rtc_datetime_t& dt)
    {
        return setDateTime(&dt.date, &dt.time);
    }
    //! @brief Set datetime (pointer)
    inline bool setDateTime(const m5::rtc_datetime_t* dt)
    {
        return dt ? setDateTime(&dt->date, &dt->time) : false;
    }
    //! @brief Set date only (pointer)
    inline bool setDate(const m5::rtc_date_t* date)
    {
        return setDateTime(date, nullptr);
    }
    //! @brief Set date only (reference)
    inline bool setDate(const m5::rtc_date_t& date)
    {
        return setDateTime(&date, nullptr);
    }
    //! @brief Set time only (pointer)
    inline bool setTime(const m5::rtc_time_t* time)
    {
        return setDateTime(nullptr, time);
    }
    //! @brief Set time only (reference)
    inline bool setTime(const m5::rtc_time_t& time)
    {
        return setDateTime(nullptr, &time);
    }

    //! @brief Get time (return by value)
    inline m5::rtc_time_t getTime()
    {
        pcf8563::rtc_time_t t{};
        readTime(t);
        return {t.hours, t.minutes, t.seconds};
    }
    //! @brief Get date (return by value)
    inline m5::rtc_date_t getDate()
    {
        pcf8563::rtc_date_t d{};
        readDate(d);
        return {d.year, d.month, d.date, d.weekDay};
    }
    //! @brief Get datetime (return by value)
    inline m5::rtc_datetime_t getDateTime()
    {
        pcf8563::rtc_datetime_t dt{};
        readDateTime(dt);
        return {{dt.date.year, dt.date.month, dt.date.date, dt.date.weekDay},
                {dt.time.hours, dt.time.minutes, dt.time.seconds}};
    }

    /*!
      @brief Set alarm by date and time
      @param date Date (can be nullptr). Negative fields are ignored.
      @param time Time (can be nullptr). Negative fields are ignored.
      @return 1 if enabled, 0 if disabled
     */
    int setAlarmIRQ(const m5::rtc_date_t* date, const m5::rtc_time_t* time);
    //! @brief Set alarm by date and time (reference)
    inline int setAlarmIRQ(const m5::rtc_date_t& date, const m5::rtc_time_t& time)
    {
        return setAlarmIRQ(&date, &time);
    }
    //! @brief Set alarm by time only
    inline int setAlarmIRQ(const m5::rtc_time_t& time)
    {
        return setAlarmIRQ(nullptr, &time);
    }
    /*!
      @brief Set timer-based alarm IRQ (deprecated, use setTimerIRQ)
      @param afterSeconds Seconds until alarm (negative to disable)
      @return Actual seconds set, or -1 if disabled
     */
    int setAlarmIRQ(const int afterSeconds);
    /*!
      @brief Set timer IRQ in milliseconds
      @param timer_msec Milliseconds until IRQ (0 to disable)
      @return Actual milliseconds set (0 if disabled)
     */
    uint32_t setTimerIRQ(const uint32_t timer_msec);

    //! @brief Get voltage low status
    inline bool getVoltLow()
    {
        bool low{};
        return readVoltLow(low) ? low : true;
    }
    //! @brief Get IRQ status (alarm or timer flag set)
    inline bool getIRQstatus()
    {
        uint8_t val{};
        if (!read_control2(val)) {
            return false;
        }
        return (val & 0x0C) != 0;
    }
    //! @brief Clear IRQ flags
    inline void clearIRQ()
    {
        write_control2_bits(0x0C, 0x00);
    }
    //! @brief Disable IRQ (clear flags and disable alarm/timer)
    void disableIRQ();
    /*!
      @brief Set system time from RTC
      @param tz Timezone (optional, can be nullptr)
     */
    void setSystemTimeFromRtc(struct timezone* tz = nullptr);
    ///@}

#endif  // defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

protected:
    ///@name I2C register access (stop bit centralized here for experimentation)
    ///@{
    bool read_register(const uint8_t reg, uint8_t* buf, const size_t len);
    bool read_register8(const uint8_t reg, uint8_t& val);
    bool write_register(const uint8_t reg, const uint8_t* buf, const size_t len);
    bool write_register8(const uint8_t reg, const uint8_t val);
    ///@}

    //! @brief Read time and/or date registers (nullable pointer support)
    bool read_datetime(pcf8563::rtc_date_t* date, pcf8563::rtc_time_t* time);
    //! @brief Write time and/or date registers (nullable pointer support)
    bool write_datetime(const pcf8563::rtc_date_t* date, const pcf8563::rtc_time_t* time);
    //! @brief Read CONTROL2 register
    bool read_control2(uint8_t& val);
    //! @brief Write CONTROL2 register with bit mask (read-modify-write)
    bool write_control2_bits(const uint8_t mask, const uint8_t bits);

private:
    static void IRAM_ATTR isr_handler(void* arg);
    void check_irq_flags();

    config_t _cfg{};
    volatile bool _isr_fired{};
};

namespace pcf8563 {
///@cond
namespace command {
constexpr uint8_t CONTROL1_REG{0x00};
constexpr uint8_t CONTROL2_REG{0x01};
constexpr uint8_t SECONDS_REG{0x02};
constexpr uint8_t MINUTES_REG{0x03};
constexpr uint8_t HOURS_REG{0x04};
constexpr uint8_t DAYS_REG{0x05};
constexpr uint8_t WEEKDAYS_REG{0x06};
constexpr uint8_t MONTHS_REG{0x07};
constexpr uint8_t YEARS_REG{0x08};
constexpr uint8_t ALARM_MINUTES_REG{0x09};
constexpr uint8_t ALARM_HOURS_REG{0x0A};
constexpr uint8_t ALARM_DAY_REG{0x0B};
constexpr uint8_t ALARM_WEEKDAY_REG{0x0C};
constexpr uint8_t CLKOUT_CONTROL_REG{0x0D};
constexpr uint8_t TIMER_CONTROL_REG{0x0E};
constexpr uint8_t TIMER_REG{0x0F};
}  // namespace command
///@endcond
}  // namespace pcf8563

}  // namespace unit
}  // namespace m5
#endif
