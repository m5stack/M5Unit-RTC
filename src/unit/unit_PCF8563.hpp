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
        //! (polls even if false when int_pin < 0)
        bool polling{true};
        //! CLKOUT pin output frequency (default: disabled)
        pcf8563::ClockOutput clkout{pcf8563::ClockOutput::None};
        //! Polling interval in milliseconds for IRQ status check in update() (polling mode)
        uint32_t polling_interval{500};
        //! Callback invoked from update() when alarm fires (nullptr: disabled)
        irq_callback_t on_alarm{nullptr};
        //! Callback invoked from update() when timer fires (nullptr: disabled)
        irq_callback_t on_timer{nullptr};
    };

    /*!
      @brief Constructor
      @param addr I2C address
     */
    explicit UnitPCF8563(const uint8_t addr = DEFAULT_ADDRESS) : Component(addr)
    {
        auto ccfg = component_config();
        // fSCL max 400 kHz on PCF8563 / BM8563 / HYM8563
        // (BM8563 datasheet lists tSU;STO min 4.0 us, but 400 kHz is used in practice, as in M5Unified)
        ccfg.clock = 400 * 1000U;
        component_config(ccfg);
    }
    //! @brief Destructor (removes the ISR handler added by begin())
    virtual ~UnitPCF8563();

    /*!
      @brief Initialize the unit with config()
      @return True if successful
      @note Clears CONTROL1/CONTROL2, sets CLKOUT and stops the timer.
            In the hardware interrupt mode it also sets up the GPIO and the ISR, and fails if they fail
     */
    virtual bool begin() override;
    /*!
      @brief Check the alarm / timer flags and invoke the callbacks
      @param force Check now, regardless of polling_interval (polling mode)
      @note Does nothing without on_alarm / on_timer
     */
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
      @param time Time (hours 0-23, minutes 0-59, seconds 0-59)
      @return True if successful, false if out of range or on failure
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
      @param date Date (year 1900-2099, month 1-12, date 1-31, weekDay 0-6 or negative to calculate it from the date)
      @return True if successful, false if out of range or on failure
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
      @param dt DateTime (ranges as writeDate() / writeTime())
      @return True if successful, false if out of range or on failure
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
      - **Polling mode** (default, `polling=true`, or `int_pin` < 0): update() reads
        the IRQ status register via I2C every polling_interval ms and invokes the callback.
        Works without the INT pin (GROVE compatible).
      - **Hardware interrupt mode** (`polling=false`, `int_pin` set): begin()
        adds an ESP-IDF GPIO ISR (gpio_isr_handler_add) on the specified pin and fails if it cannot.
        The ISR sets an internal flag; update() detects it and invokes the callback.
        Requires a direct connection from the PCF8563 INT pin to a host GPIO.

      @note In both modes, callbacks (on_alarm / on_timer) are called from update(),
      never from an ISR context. Set them in config_t before calling begin().
      update() does nothing without callbacks.

      @note update() clears only the flags that have a callback; the others are left for
      readAlarmFlag() / readTimerFlag(). In the hardware interrupt mode a flag with its interrupt
      enabled is cleared too, since it would hold INT low and no further interrupt would arrive.

      @note A flag is cleared by read-modify-write of CONTROL2, writing the other flag back as read
      (on the Unit RTC writing 1 sets a flag, unlike the datasheets). The other flag set between
      the read and the write is lost.
     */
    ///@name Alarm/Timer
    ///@{
    /*!
      @brief Set alarm and enable interrupt
      @param time Alarm time (minutes, hours). Negative fields are disabled.
      @param date Alarm date (date, weekDay). Negative fields are disabled.
      @return True if successful, false if a non-negative field is out of range
              (minutes 0-59, hours 0-23, date 1-31, weekDay 0-6) or on failure
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
      @brief Set the countdown timer with a millisecond period
      @param msec Period in milliseconds, rounded to seconds (0 or less than 500 to disable)
      @return Actual period in milliseconds (0 if disabled or on failure)
      @note The timer repeats: the PCF8563 reloads the countdown every period and sets TF each time
            (it has no one-shot mode). Stop it with writeTimer(0), e.g. in the on_timer callback.
      @note Automatically selects Hz1 (< 270s) or HzPM (>= 270s) clock source.
            Clears the timer flag (the alarm flag is kept), enables timer and timer interrupt.
            The /INT output mode (TI_TP) is kept. Maximum: 255 minutes (15,300,000 ms).
     */
    uint32_t writeTimer(const uint32_t msec);
    /*!
      @brief Set the countdown timer (the repeat argument is ignored)
      @deprecated The PCF8563 has no one-shot timer. Use writeTimer(msec), and stop it with writeTimer(0)
     */
    [[deprecated("The PCF8563 has no one-shot timer. Use writeTimer(msec) and stop it with writeTimer(0)")]] uint32_t
    writeTimer(const uint32_t msec, const bool repeat)
    {
        (void)repeat;
        return writeTimer(msec);
    }
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
      @brief Read the /INT output mode of the timer (TI_TP bit)
      @param[out] pulse True if /INT pulses every countdown, false if /INT follows TF (stays active until cleared)
      @return True if successful
      @note The countdown timer always repeats regardless of this bit
     */
    bool readTimerInterruptPulse(bool& pulse);
    /*!
      @brief Write the /INT output mode of the timer (TI_TP bit)
      @param pulse True to pulse /INT every countdown, false to make /INT follow TF (stays active until cleared)
      @return True if successful
      @note The countdown timer always repeats regardless of this bit
     */
    bool writeTimerInterruptPulse(const bool pulse);
    //! @deprecated TI_TP selects the /INT output mode, not periodic / one-shot. Use readTimerInterruptPulse()
    [[deprecated("TI_TP selects the /INT output mode, not periodic / one-shot. Use readTimerInterruptPulse()")]] bool
    readTimerPeriodic(bool& periodic)
    {
        return readTimerInterruptPulse(periodic);
    }
    //! @deprecated TI_TP selects the /INT output mode, not periodic / one-shot. Use writeTimerInterruptPulse()
    [[deprecated("TI_TP selects the /INT output mode, not periodic / one-shot. Use writeTimerInterruptPulse()")]] bool
    writeTimerPeriodic(const bool periodic)
    {
        return writeTimerInterruptPulse(periodic);
    }
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
      @return 1 if enabled, 0 if disabled or on failure
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
      @brief Set the timer IRQ in seconds (same as setTimerIRQ(afterSeconds * 1000))
      @param afterSeconds Timer period in seconds (negative to disable), limited to UINT32_MAX / 1000
      @return Actual seconds set, -1 if disabled by a negative value, 0 if disabled by 0 or on failure
      @note The timer repeats; see writeTimer()
     */
    int setAlarmIRQ(const int afterSeconds);
    /*!
      @brief Set timer IRQ in milliseconds
      @param timer_msec Timer period in milliseconds (0 to disable)
      @return Actual milliseconds set (0 if disabled or on failure)
      @note The timer repeats; see writeTimer()
     */
    uint32_t setTimerIRQ(const uint32_t timer_msec);

    //! @brief Get voltage low status (true also on I2C failure)
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
    // I2C register access (STOP between the address and data phases, see READ_STOP / WRITE_STOP)
    bool read_register(const uint8_t reg, uint8_t* buf, const size_t len);
    bool read_register8(const uint8_t reg, uint8_t& val);
    bool write_register(const uint8_t reg, const uint8_t* buf, const size_t len);
    bool write_register8(const uint8_t reg, const uint8_t val);

    // Read time and/or date registers (nullable pointer support)
    bool read_datetime(pcf8563::rtc_date_t* date, pcf8563::rtc_time_t* time);
    // Write time and/or date registers (nullable pointer support)
    bool write_datetime(const pcf8563::rtc_date_t* date, const pcf8563::rtc_time_t* time);
    // Read CONTROL2 register
    bool read_control2(uint8_t& val);
    // Write CONTROL2 register with bit mask (read-modify-write)
    bool write_control2_bits(const uint8_t mask, const uint8_t bits);

private:
    static void IRAM_ATTR isr_handler(void* arg);
    void check_irq_flags();
    void remove_isr();

    config_t _cfg{};
    volatile bool _isr_fired{};
    int16_t _isr_pin{-1};  // Pin with the ISR handler added by begin() (-1: none)
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
