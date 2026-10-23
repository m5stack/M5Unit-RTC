/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_RX8130.hpp
  @brief RX8130CE for M5UnitUnified
 */
#ifndef M5_UNIT_RTC_UNIT_RX8130_HPP
#define M5_UNIT_RTC_UNIT_RX8130_HPP

#include <M5UnitComponent.hpp>
#include "unit_RX8130_types.hpp"

namespace m5 {
namespace unit {

/*!
  @class UnitRX8130
  @brief Real-time clock using Epson RX8130CE
  @details The RX8130CE is built into several M5Stack controllers (Tab5, StampPLC, PaperColor, etc.)
  on the internal I2C bus. Add it with the internal bus, e.g. `Units.add(rtc, M5.In_I2C)`, and set
  `internal_rtc = false` in M5.config() so that M5.Rtc does not use the same chip.
  @note The year range is 2000-2099 (the chip has no century bit)
 */
class UnitRX8130 : public Component {
    M5_UNIT_COMPONENT_HPP_BUILDER(UnitRX8130, 0x32);

public:
    //! @brief IRQ callback type
    using irq_callback_t = void (*)();

    /*!
      @struct config_t
      @brief Settings for begin
     */
    struct config_t {
        //! GPIO pin connected to RX8130 /IRQ pin (-1: not connected)
        int16_t int_pin{-1};
        //! true: poll IRQ status via I2C in update(), false: use hardware interrupt via int_pin
        bool polling{true};
        //! FOUT pin output frequency (default: off)
        rx8130::ClockOutput fout{rx8130::ClockOutput::None};
        /*!
          Enable the power switchover function (INIEN)
          @note Once enabled, the switchover keeps working until the chip is powered off,
          even if this bit is cleared
         */
        bool power_switch{true};
        /*!
          Enable charging of the backup battery from VDD (CHGEN)
          @warning Enable it only when a rechargeable battery or a supercapacitor is on VBAT.
          Charging a primary cell can make it leak or burst
          @note Built-in RX8130CE of M5Stack boards (from the published schematics):
          - Tab5, StampPLC: supercapacitor / rechargeable cell through 1 kOhm, set true to keep the backup charged
          - ToughC5: CR2032 primary cell directly on VBAT, keep false
          - StopWatch, PaperColor, PaperMono: no backup cell, no effect
          @note The default is false (the power-on state of the chip)
         */
        bool charge{false};
        //! Polling interval in milliseconds for IRQ status check in update()
        uint32_t polling_interval{500};
        //! Callback invoked from update() when alarm fires (nullptr: disabled)
        irq_callback_t on_alarm{nullptr};
        //! Callback invoked from update() when wakeup timer fires (nullptr: disabled)
        irq_callback_t on_timer{nullptr};
        //! Callback invoked from update() when time update interrupt fires (nullptr: disabled)
        irq_callback_t on_update{nullptr};
    };

    /*!
      @brief Constructor
      @param addr I2C address
     */
    explicit UnitRX8130(const uint8_t addr = DEFAULT_ADDRESS) : Component(addr)
    {
        auto ccfg = component_config();
        // fSCL max 400 kHz
        ccfg.clock = 400 * 1000U;
        component_config(ccfg);
    }
    //! @brief Destructor (removes the ISR handler added by begin())
    virtual ~UnitRX8130();

    /*!
      @brief Initialize the unit with config()
      @return True if successful
      @note If the oscillation stop flag (VLF) is set, the registers with undefined values are initialized
            (the time stays invalid until writeDateTime()). Interrupts are disabled, the flags are kept so that
            the wakeup source can be read. In the hardware interrupt mode it also sets up the GPIO and the ISR,
            and fails if they fail
     */
    virtual bool begin() override;
    /*!
      @brief Check the alarm / timer / update flags and invoke the callbacks
      @param force Check now, regardless of polling_interval (polling mode)
      @note Does nothing without on_alarm / on_timer / on_update
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
    bool readTime(rx8130::rtc_time_t& time);
    /*!
      @brief Write time to RTC
      @param time Time (hours 0-23, minutes 0-59, seconds 0-59)
      @return True if successful, false if out of range or on failure
      @note Writing the seconds clears the sub-second counter.
            The oscillation stop flag (VLF) is kept, as the date may still be invalid; use writeDateTime()
     */
    bool writeTime(const rx8130::rtc_time_t& time);
    /*!
      @brief Read date from RTC
      @param[out] date Date (year, month, date, weekDay)
      @return True if successful
     */
    bool readDate(rx8130::rtc_date_t& date);
    /*!
      @brief Write date to RTC
      @param date Date (year 2000-2099, month 1-12, date 1-31, weekDay 0-6)
      @return True if successful, false if out of range or on failure
      @note If weekDay is negative, it is calculated from the date.
            The oscillation stop flag (VLF) is kept, as the time may still be invalid; use writeDateTime()
     */
    bool writeDate(const rx8130::rtc_date_t& date);
    /*!
      @brief Read date and time from RTC
      @param[out] dt DateTime
      @return True if successful
     */
    bool readDateTime(rx8130::rtc_datetime_t& dt);
    /*!
      @brief Write date and time to RTC
      @param dt DateTime (ranges as writeDate() / writeTime())
      @return True if successful, false if out of range or on failure
      @note Clears the oscillation stop flag (VLF). It cannot be cleared until the oscillation starts,
            so this retries for up to 1 s and returns false if VLF stays set
     */
    bool writeDateTime(const rx8130::rtc_datetime_t& dt);
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
      @note About IRQ/Alarm/Timer/Update:
      These functions configure the RX8130 alarm, wakeup timer and time update interrupt.
      When triggered, the chip drives its /IRQ pin (open-drain, active LOW).

      @note Two modes are supported via config_t:
      - **Polling mode** (default, `polling=true`): update() periodically reads
        the flag register via I2C and invokes the callback.
      - **Hardware interrupt mode** (`polling=false`, `int_pin` set): the ISR sets an internal
        flag; update() detects it and invokes the callback. Requires a connection from the
        /IRQ pin to a host GPIO.

      @note In both modes, callbacks are called from update(), never from an ISR context.
      Set them in config_t before calling begin(). update() does nothing without callbacks.

      @note update() clears only the flags that have a callback; the others are left for the read functions.
      In the hardware interrupt mode a flag with its interrupt enabled is cleared too
      (AF holds /IRQ low, and no further interrupt would arrive).
     */
    ///@name Alarm
    ///@{
    /*!
      @brief Set alarm and enable interrupt
      @param time Alarm time (minutes, hours). Negative fields are disabled.
      @param date Alarm date (date or weekDay). Negative fields are disabled.
      @return True if successful
      @note If both date and weekDay are set, date is used (the chip compares either day or week)
      @note Clears alarm flag, writes alarm registers, and enables alarm interrupt
            if any field is non-negative. Disables alarm interrupt if all fields are negative.
     */
    bool writeAlarm(const rx8130::rtc_time_t& time, const rx8130::rtc_date_t& date);
    /*!
      @brief Set weekly alarm on multiple weekdays and enable interrupt
      @param time Alarm time (minutes, hours). Negative fields are disabled.
      @param weekdays Weekday bits (bit0:Sun ... bit6:Sat). 0 disables the weekday comparison
      @return True if successful
     */
    bool writeAlarmWeekdays(const rx8130::rtc_time_t& time, const uint8_t weekdays);
    /*!
      @brief Read alarm settings
      @param[out] time Alarm time (minutes, hours; seconds always -1). Disabled fields are -1.
      @param[out] date Alarm date (date, weekDay; year/month unused). Disabled fields are -1.
      @return True if successful
      @note If several weekdays are set, weekDay is -1. Use readAlarmWeekdays()
     */
    bool readAlarm(rx8130::rtc_time_t& time, rx8130::rtc_date_t& date);
    /*!
      @brief Read alarm weekday bits
      @param[out] weekdays Weekday bits (bit0:Sun ... bit6:Sat). 0 if day alarm or disabled
      @return True if successful
     */
    bool readAlarmWeekdays(uint8_t& weekdays);
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
    ///@}

    ///@name Wakeup timer
    ///@{
    /*!
      @brief Set the wakeup timer with a millisecond period
      @param msec Period in milliseconds (0 to disable)
      @return Actual period in milliseconds (0 if disabled or failed)
      @note The timer repeats: the RX8130 reloads the preset every period and sets TF each time
            (it has no one-shot mode). Stop it with writeTimer(0), e.g. in the on_timer callback.
      @note Selects the source clock automatically. Clears timer flag, enables timer and timer interrupt.
     */
    uint32_t writeTimer(const uint32_t msec);
    /*!
      @brief Read timer control settings
      @param[out] enabled True if timer is enabled (TE bit)
      @param[out] clock Source clock (TSEL bits)
      @return True if successful
     */
    bool readTimerControl(bool& enabled, rx8130::TimerClock& clock);
    /*!
      @brief Write timer control settings
      @param enabled True to enable timer (TE bit)
      @param clock Source clock (TSEL bits)
      @return True if successful
      @note Setting TE from 0 to 1 starts counting down from the preset value
     */
    bool writeTimerControl(const bool enabled, const rx8130::TimerClock clock);
    /*!
      @brief Read timer counter
      @param[out] count Preset value (TE=0) or current countdown value (TE=1)
      @return True if successful
     */
    bool readTimerValue(uint16_t& count);
    /*!
      @brief Write timer preset value
      @param count Preset value (1-65535)
      @return True if successful
      @note Fails while the timer is running (TE=1)
     */
    bool writeTimerValue(const uint16_t count);
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
      @brief Read timer pause state (TSTP bit)
      @param[out] paused True if countdown is paused
      @return True if successful
     */
    bool readTimerPause(bool& paused);
    /*!
      @brief Write timer pause state (TSTP bit)
      @param pause True to pause countdown, false to resume from the paused value
      @return True if successful
      @note Ignored by the chip unless the count mode is TimerCountMode::Always
     */
    bool writeTimerPause(const bool pause);
    /*!
      @brief Read timer count mode (TBKE, TBKON bits)
      @param[out] mode Count mode
      @return True if successful
     */
    bool readTimerCountMode(rx8130::TimerCountMode& mode);
    /*!
      @brief Write timer count mode (TBKE, TBKON bits)
      @param mode Count mode
      @return True if successful
      @note Used to measure the operating time on the main or backup power supply
     */
    bool writeTimerCountMode(const rx8130::TimerCountMode mode);
    ///@}

    ///@name Time update interrupt
    ///@{
    /*!
      @brief Read update interrupt timing (USEL bit)
      @param[out] timing Second or minute
      @return True if successful
     */
    bool readUpdateTiming(rx8130::UpdateTiming& timing);
    /*!
      @brief Write update interrupt timing (USEL bit)
      @param timing Second or minute
      @return True if successful
     */
    bool writeUpdateTiming(const rx8130::UpdateTiming timing);
    /*!
      @brief Read update interrupt enable state
      @param[out] enabled True if update interrupt is enabled (UIE bit)
      @return True if successful
     */
    bool readUpdateInterrupt(bool& enabled);
    /*!
      @brief Write update interrupt enable state
      @param enabled True to enable update interrupt (UIE bit)
      @return True if successful
      @note The /IRQ output is released automatically after about 7.57 ms
     */
    bool writeUpdateInterrupt(const bool enabled);
    /*!
      @brief Read update flag
      @param[out] fired True if update event occurred (UF bit)
      @return True if successful
     */
    bool readUpdateFlag(bool& fired);
    /*!
      @brief Clear update flag (UF bit)
      @return True if successful
     */
    bool clearUpdateFlag();
    ///@}

    ///@name Clock control
    ///@{
    /*!
      @brief Read RTC clock stop state
      @param[out] stopped True if timekeeping is stopped (STOP bit)
      @return True if successful
     */
    bool readStop(bool& stopped);
    /*!
      @brief Write RTC clock stop state
      @param stop True to stop timekeeping, false to start
      @return True if successful
      @note While stopped, alarm and update interrupts do not occur and the power switchover does not work
     */
    bool writeStop(const bool stop);
    /*!
      @brief Read FOUT output frequency
      @param[out] fout Frequency (FSEL bits)
      @return True if successful
     */
    bool readClockOutput(rx8130::ClockOutput& fout);
    /*!
      @brief Write FOUT output frequency
      @param fout Frequency (FSEL bits)
      @return True if successful
     */
    bool writeClockOutput(const rx8130::ClockOutput fout);
    /*!
      @brief Read digital offset
      @param[out] enabled True if digital offset is enabled (DTE bit)
      @param[out] steps Offset in steps of 3.05 ppm (exactly 3.0518, see DIGITAL_OFFSET_STEP_PPM) (-64 to 63)
      @return True if successful
     */
    bool readDigitalOffset(bool& enabled, int8_t& steps);
    /*!
      @brief Write digital offset
      @param enabled True to enable digital offset (DTE bit)
      @param steps Offset in steps of 3.05 ppm (exactly 3.0518, see DIGITAL_OFFSET_STEP_PPM) (-64 to 63).
             Positive makes the clock faster
      @return True if successful
     */
    bool writeDigitalOffset(const bool enabled, const int8_t steps);
    /*!
      @brief Write digital offset in ppm
      @param ppm Offset in ppm (-195.31 to +192.26). 0 disables the digital offset
      @return True if successful
      @note The value is rounded to the nearest step (DIGITAL_OFFSET_STEP_PPM), as in the datasheet examples
     */
    bool writeDigitalOffsetPPM(const float ppm);
    ///@}

    ///@name Status
    ///@{
    /*!
      @brief Read oscillation stop flag
      @param[out] low True if oscillation stop was detected (VLF bit). Time data are invalid
      @return True if successful
      @note Cleared when the date and time are written together (writeDateTime()), or by clearVoltLow()
     */
    bool readVoltLow(bool& low);
    /*!
      @brief Clear oscillation stop flag (VLF bit)
      @return True if successful
     */
    bool clearVoltLow();
    /*!
      @brief Read backup battery low flag
      @param[out] low True if low VBAT was detected (VBLF bit)
      @return True if successful
     */
    bool readBatteryLow(bool& low);
    /*!
      @brief Clear backup battery low flag (VBLF bit)
      @return True if successful
     */
    bool clearBatteryLow();
    /*!
      @brief Read backup battery full charge flag
      @param[out] full True if the full charge voltage is reached (VBFF bit)
      @return True if successful
     */
    bool readBatteryFull(bool& full);
    /*!
      @brief Read reset detection flag
      @param[out] detected True if VDD dropped below VDET1 (RSF bit)
      @return True if successful
     */
    bool readResetFlag(bool& detected);
    /*!
      @brief Clear reset detection flag (RSF bit)
      @return True if successful
     */
    bool clearResetFlag();
    ///@}

    ///@name Backup power supply
    ///@{
    /*!
      @brief Read power switchover state (INIEN bit)
      @param[out] enabled True if power switchover is enabled
      @return True if successful
     */
    bool readPowerSwitch(bool& enabled);
    /*!
      @brief Write power switchover state (INIEN bit)
      @param enabled True to enable power switchover
      @return True if successful
      @note Once enabled, the switchover keeps working even if this bit is cleared
     */
    bool writePowerSwitch(const bool enabled);
    /*!
      @brief Read backup battery charge state (CHGEN bit)
      @param[out] enabled True if charging is enabled
      @return True if successful
     */
    bool readBatteryCharge(bool& enabled);
    /*!
      @brief Write backup battery charge state (CHGEN bit)
      @param enabled True to enable charging (rechargeable battery only)
      @return True if successful
     */
    bool writeBatteryCharge(const bool enabled);
    /*!
      @brief Read full charge detection voltage (BFVSEL bits)
      @param[out] voltage Voltage
      @return True if successful
     */
    bool readFullChargeVoltage(rx8130::FullChargeVoltage& voltage);
    /*!
      @brief Write full charge detection voltage (BFVSEL bits)
      @param voltage Voltage
      @return True if successful
     */
    bool writeFullChargeVoltage(const rx8130::FullChargeVoltage voltage);
    /*!
      @brief Read VDD reset detection voltage (RSVSEL bit)
      @param[out] voltage Voltage
      @return True if successful
     */
    bool readResetVoltage(rx8130::ResetVoltage& voltage);
    /*!
      @brief Write VDD reset detection voltage (RSVSEL bit)
      @param voltage Voltage
      @return True if successful
     */
    bool writeResetVoltage(const rx8130::ResetVoltage voltage);
    /*!
      @brief Read SW1 off time for voltage detection (SMPTSEL bits)
      @param[out] time Off time
      @return True if successful
     */
    bool readDetectionTime(rx8130::DetectionTime& time);
    /*!
      @brief Write SW1 off time for voltage detection (SMPTSEL bits)
      @param time Off time
      @return True if successful
      @note A longer time detects slower VDD drops but lowers the charging efficiency
     */
    bool writeDetectionTime(const rx8130::DetectionTime time);
    /*!
      @brief Read low VBAT detection state (VBLFE bit)
      @param[out] enabled True if low VBAT detection is always available on VDD supply
      @return True if successful
     */
    bool readBatteryLowDetection(bool& enabled);
    /*!
      @brief Write low VBAT detection state (VBLFE bit)
      @param enabled True to detect low VBAT on VDD supply even if not charging
      @return True if successful
      @note Requires power switchover (INIEN) to have been enabled at least once
     */
    bool writeBatteryLowDetection(const bool enabled);
    ///@}

    ///@name User RAM
    ///@{
    /*!
      @brief Read user RAM
      @param[out] buf Buffer
      @param offset Offset in RAM (0-3)
      @param len Length (offset + len <= 4)
      @return True if successful
     */
    bool readUserRAM(uint8_t* buf, const uint8_t offset, const uint8_t len);
    /*!
      @brief Write user RAM
      @param buf Data
      @param offset Offset in RAM (0-3)
      @param len Length (offset + len <= 4)
      @return True if successful
     */
    bool writeUserRAM(const uint8_t* buf, const uint8_t offset, const uint8_t len);
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
        rx8130::rtc_date_t d;
        rx8130::rtc_time_t t;
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
      @note The oscillation stop flag (VLF, see readVoltLow()) is cleared only when both date and time are
            given. setDate() / setTime() alone keep it
     */
    inline bool setDateTime(const m5::rtc_date_t* date, const m5::rtc_time_t* time)
    {
        rx8130::rtc_date_t d;
        rx8130::rtc_time_t t;
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
    //! @brief Set date only (pointer, VLF is kept)
    inline bool setDate(const m5::rtc_date_t* date)
    {
        return setDateTime(date, nullptr);
    }
    //! @brief Set date only (reference, VLF is kept)
    inline bool setDate(const m5::rtc_date_t& date)
    {
        return setDateTime(&date, nullptr);
    }
    //! @brief Set time only (pointer, VLF is kept)
    inline bool setTime(const m5::rtc_time_t* time)
    {
        return setDateTime(nullptr, time);
    }
    //! @brief Set time only (reference, VLF is kept)
    inline bool setTime(const m5::rtc_time_t& time)
    {
        return setDateTime(nullptr, &time);
    }

    //! @brief Get time (return by value)
    inline m5::rtc_time_t getTime()
    {
        rx8130::rtc_time_t t{};
        readTime(t);
        return {t.hours, t.minutes, t.seconds};
    }
    //! @brief Get date (return by value)
    inline m5::rtc_date_t getDate()
    {
        rx8130::rtc_date_t d{};
        readDate(d);
        return {d.year, d.month, d.date, d.weekDay};
    }
    //! @brief Get datetime (return by value)
    inline m5::rtc_datetime_t getDateTime()
    {
        rx8130::rtc_datetime_t dt{};
        readDateTime(dt);
        return {{dt.date.year, dt.date.month, dt.date.date, dt.date.weekDay},
                {dt.time.hours, dt.time.minutes, dt.time.seconds}};
    }

    /*!
      @brief Set alarm by date and time
      @param date Date (can be nullptr). Negative fields are ignored. date takes precedence over weekDay
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

    //! @brief Get backup battery low status (VBLF, same as M5Unified; true also on I2C failure)
    inline bool getVoltLow()
    {
        bool low{};
        return readBatteryLow(low) ? low : true;
    }
    //! @brief Get IRQ status (alarm or timer flag set)
    bool getIRQstatus();
    //! @brief Clear IRQ flags (alarm and timer)
    void clearIRQ();
    //! @brief Disable IRQ (AIE / TIE / UIE = 0, stop the wakeup timer (TE=0) and clear AF / TF / UF)
    void disableIRQ();
    /*!
      @brief Set system time from RTC
      @param tz Timezone (optional, can be nullptr)
     */
    void setSystemTimeFromRtc(struct timezone* tz = nullptr);
    ///@}

#endif  // defined(__M5_RTC_BASE_H__) || defined(__M5UNIFIED_HPP__)

protected:
    // I2C register access
    bool read_register(const uint8_t reg, uint8_t* buf, const size_t len);
    bool read_register8(const uint8_t reg, uint8_t& val);
    bool write_register(const uint8_t reg, const uint8_t* buf, const size_t len);
    bool write_register8(const uint8_t reg, const uint8_t val);
    // Read-modify-write of the masked bits
    bool write_register_bits(const uint8_t reg, const uint8_t mask, const uint8_t bits);

    // Read time and/or date registers (nullable pointer support)
    bool read_datetime(rx8130::rtc_date_t* date, rx8130::rtc_time_t* time);
    // Write time and/or date registers (nullable pointer support)
    bool write_datetime(const rx8130::rtc_date_t* date, const rx8130::rtc_time_t* time);
    // Read a bit of a register
    bool read_bit(const uint8_t reg, const uint8_t bit, bool& on);
    // Clear flags of the flag register (write 0 to clear)
    bool clear_flags(const uint8_t bits);
    // Clear VLF, retrying until the oscillation starts
    bool clear_vlf();
    // Stop the wakeup timer (TE=0, TIE=0)
    bool stop_timer();
    // Write alarm registers and WADA, then AIE
    bool write_alarm(const uint8_t min, const uint8_t hour, const uint8_t day_week, const bool wada, const bool enable);

private:
    static void IRAM_ATTR isr_handler(void* arg);
    void check_irq_flags();
    void remove_isr();

    config_t _cfg{};
    volatile bool _isr_fired{};
    int16_t _isr_pin{-1};  // Pin with the ISR handler added by begin() (-1: none)
};

namespace rx8130 {
///@cond
namespace command {
constexpr uint8_t SECONDS_REG{0x10};
constexpr uint8_t MINUTES_REG{0x11};
constexpr uint8_t HOURS_REG{0x12};
constexpr uint8_t WEEK_REG{0x13};
constexpr uint8_t DAY_REG{0x14};
constexpr uint8_t MONTH_REG{0x15};
constexpr uint8_t YEAR_REG{0x16};
constexpr uint8_t ALARM_MINUTES_REG{0x17};
constexpr uint8_t ALARM_HOURS_REG{0x18};
constexpr uint8_t ALARM_WEEK_DAY_REG{0x19};
constexpr uint8_t TIMER_COUNTER0_REG{0x1A};
constexpr uint8_t TIMER_COUNTER1_REG{0x1B};
constexpr uint8_t EXTENSION_REG{0x1C};
constexpr uint8_t FLAG_REG{0x1D};
constexpr uint8_t CONTROL0_REG{0x1E};
constexpr uint8_t CONTROL1_REG{0x1F};
constexpr uint8_t USER_RAM_REG{0x20};
constexpr uint8_t DIGITAL_OFFSET_REG{0x30};
constexpr uint8_t EXTENSION1_REG{0x31};
}  // namespace command
///@endcond
}  // namespace rx8130

}  // namespace unit
}  // namespace m5
#endif
