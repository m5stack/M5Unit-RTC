/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  UnitTest for UnitRX8130 (built-in RX8130CE on the internal I2C bus)
*/
#include <gtest/gtest.h>
#include <Wire.h>
#include <M5Unified.h>
#include <M5UnitUnified.hpp>
#include <googletest/test_template.hpp>
#include <googletest/test_helper.hpp>
#include <unit/unit_RX8130.hpp>
#include <sys/time.h>
#include <cstring>

using namespace m5::unit::googletest;
using namespace m5::unit;
using namespace m5::unit::rx8130;

class TestRX8130 : public I2CComponentTestBase<UnitRX8130> {
protected:
    virtual UnitRX8130* get_instance() override
    {
        return new m5::unit::UnitRX8130();
    }

    // The RX8130CE is on the internal bus, not on the GROVE port
    virtual bool begin() override
    {
        if (!M5.In_I2C.isEnabled()) {
            M5_LOGE("In_I2C is not enabled");
            return false;
        }
        return Units.add(*unit, M5.In_I2C) && Units.begin();
    }

    // Keep the board clock: save the date/time and restore it (plus the elapsed time) after each test
    // Interrupts are disabled before and after each test so that /IRQ is never left low
    virtual void SetUp() override
    {
        I2CComponentTestBase<UnitRX8130>::SetUp();
        if (unit) {
            unit->disableIRQ();
            struct tm t {
            };
            _saved    = unit->readDateTime(t);
            _saved_at = mktime(&t);
            _saved_ms = m5::utility::millis();
        }
    }
    virtual void TearDown() override
    {
        if (unit) {
            unit->disableIRQ();
            unit->writeStop(false);
            if (_saved) {
                time_t now  = _saved_at + (m5::utility::millis() - _saved_ms + 500) / 1000;
                struct tm t = *localtime(&now);
                unit->writeDateTime(t);
            }
        }
        I2CComponentTestBase<UnitRX8130>::TearDown();
    }

    // On Tab5 the /IRQ output drives the power controller (E_TRG), so it must stay Hi-Z.
    // There, events are checked by the flags only (with AIE/TIE/UIE = 0 the flags are set but /IRQ stays Hi-Z)
    bool irq_output_allowed() const
    {
        return M5.getBoard() != m5::board_t::board_M5Tab5;
    }

    // writeTimer() always sets TIE. Where /IRQ is not allowed, pause the countdown (TSTP) until TIE is cleared
    uint32_t start_timer(const uint32_t msec, const bool repeat)
    {
        if (irq_output_allowed()) {
            return unit->writeTimer(msec, repeat);
        }
        EXPECT_TRUE(unit->writeTimerCountMode(TimerCountMode::Always));  // TSTP is ignored when TBKE = 1
        EXPECT_TRUE(unit->writeTimerPause(true));
        auto actual = unit->writeTimer(msec, repeat);
        EXPECT_TRUE(unit->writeTimerInterrupt(false));
        EXPECT_TRUE(unit->writeTimerPause(false));
        return actual;
    }

    // Disable the alarm interrupt output where /IRQ is not allowed (the alarm flag is still set)
    void mask_alarm_output()
    {
        if (!irq_output_allowed()) {
            EXPECT_TRUE(unit->writeAlarmInterrupt(false));
        }
    }

    // Disable the wakeup timer interrupt output where /IRQ is not allowed (the timer flag is still set)
    void mask_timer_output()
    {
        if (!irq_output_allowed()) {
            EXPECT_TRUE(unit->writeTimerInterrupt(false));
        }
    }

    // Where /IRQ is not allowed, stop the clock (STOP) while an interrupt enable bit is set for settings tests.
    // STOP = 1 stops alarm and time update events, and the wakeup timer except at 4096 Hz.
    // The time is restored in TearDown
    void hold_clock(const bool hold)
    {
        if (!irq_output_allowed()) {
            EXPECT_TRUE(unit->writeStop(hold));
        }
    }

    bool _saved{};
    time_t _saved_at{};
    uint32_t _saved_ms{};
};

namespace {
// Wait for a flag with polling (timeout in ms)
template <typename F>
bool wait_flag(F&& read_flag, const uint32_t timeout_ms)
{
    auto timeout_at = m5::utility::millis() + timeout_ms;
    do {
        bool fired{};
        if (read_flag(fired) && fired) {
            return true;
        }
        m5::utility::delay(10);
    } while (m5::utility::millis() <= timeout_at);
    return false;
}
}  // namespace

// ============================================================
// DateTime
// ============================================================

TEST_F(TestRX8130, DateTime)
{
    SCOPED_TRACE(ustr);

    // --- Time round-trip ---
    {
        rtc_time_t write_vals[] = {
            {0, 0, 0},
            {12, 30, 45},
            {23, 59, 58},
        };
        for (auto& wt : write_vals) {
            auto s = m5::utility::formatString("Time %d:%d:%d", wt.hours, wt.minutes, wt.seconds);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeTime(wt));

            rtc_time_t rt{};
            EXPECT_TRUE(unit->readTime(rt));
            EXPECT_EQ(rt.hours, wt.hours);
            EXPECT_EQ(rt.minutes, wt.minutes);
            EXPECT_EQ(rt.seconds, wt.seconds);
        }
    }

    // --- Date round-trip ---
    {
        rtc_date_t write_vals[] = {
            {2000, 1, 1, 6},
            {2026, 2, 26, 4},
            {2099, 12, 31, 4},
        };
        for (auto& wd : write_vals) {
            auto s = m5::utility::formatString("Date %d-%d-%d W:%d", wd.year, wd.month, wd.date, wd.weekDay);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeDate(wd));

            rtc_date_t rd{};
            EXPECT_TRUE(unit->readDate(rd));
            EXPECT_EQ(rd.year, wd.year);
            EXPECT_EQ(rd.month, wd.month);
            EXPECT_EQ(rd.date, wd.date);
            EXPECT_EQ(rd.weekDay, wd.weekDay);
        }
    }

    // --- Weekday is calculated when negative ---
    {
        SCOPED_TRACE("Weekday auto");
        EXPECT_TRUE(unit->writeDate(rtc_date_t(2026, 10, 22, -1)));  // Thursday
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readDate(rd));
        EXPECT_EQ(rd.weekDay, 4);

        EXPECT_TRUE(unit->writeDate(rtc_date_t(2024, 2, 29, -1)));  // Thursday (leap day)
        EXPECT_TRUE(unit->readDate(rd));
        EXPECT_EQ(rd.weekDay, 4);
    }

    // --- DateTime and struct tm ---
    {
        SCOPED_TRACE("DateTime");
        rtc_datetime_t wdt(rtc_date_t(2026, 10, 22, 4), rtc_time_t(10, 20, 30));
        EXPECT_TRUE(unit->writeDateTime(wdt));

        rtc_datetime_t rdt{};
        EXPECT_TRUE(unit->readDateTime(rdt));
        EXPECT_EQ(rdt, wdt);
        EXPECT_EQ(rdt.date.weekDay, 4);

        struct tm t {
        };
        EXPECT_TRUE(unit->readDateTime(t));
        EXPECT_EQ(t.tm_year, 126);
        EXPECT_EQ(t.tm_mon, 9);
        EXPECT_EQ(t.tm_mday, 22);
        EXPECT_EQ(t.tm_hour, 10);
        EXPECT_EQ(t.tm_min, 20);

        t.tm_hour = 1;
        EXPECT_TRUE(unit->writeDateTime(t));
        EXPECT_TRUE(unit->readDateTime(rdt));
        EXPECT_EQ(rdt.time.hours, 1);
    }

    // --- Clock runs ---
    {
        SCOPED_TRACE("Running");
        EXPECT_TRUE(unit->writeTime(rtc_time_t(12, 0, 0)));
        m5::utility::delay(2100);
        rtc_time_t rt{};
        EXPECT_TRUE(unit->readTime(rt));
        EXPECT_EQ(rt.seconds, 2);
    }

    // --- Carry over to the next day ---
    {
        SCOPED_TRACE("Carry");
        EXPECT_TRUE(unit->writeDateTime(rtc_datetime_t(rtc_date_t(2099, 12, 31, 4), rtc_time_t(23, 59, 59))));
        m5::utility::delay(1100);
        rtc_datetime_t rdt{};
        EXPECT_TRUE(unit->readDateTime(rdt));
        EXPECT_EQ(rdt.date.year, 2000);  // 2-digit year wraps
        EXPECT_EQ(rdt.date.month, 1);
        EXPECT_EQ(rdt.date.date, 1);
        EXPECT_EQ(rdt.date.weekDay, 5);
        EXPECT_EQ(rdt.time.hours, 0);
    }

    // --- Invalid values ---
    {
        SCOPED_TRACE("Invalid");
        EXPECT_FALSE(unit->writeTime(rtc_time_t(24, 0, 0)));
        EXPECT_FALSE(unit->writeTime(rtc_time_t(0, 60, 0)));
        EXPECT_FALSE(unit->writeTime(rtc_time_t(0, 0, 60)));
        EXPECT_FALSE(unit->writeTime(rtc_time_t(-1, 0, 0)));
        EXPECT_FALSE(unit->writeDate(rtc_date_t(1999, 12, 31, 5)));
        EXPECT_FALSE(unit->writeDate(rtc_date_t(2100, 1, 1, 5)));
        EXPECT_FALSE(unit->writeDate(rtc_date_t(2026, 13, 1, 0)));
        EXPECT_FALSE(unit->writeDate(rtc_date_t(2026, 1, 0, 0)));
        EXPECT_FALSE(unit->writeDate(rtc_date_t(2026, 1, 1, 7)));
    }
}

// ============================================================
// Alarm
// ============================================================

TEST_F(TestRX8130, Alarm)
{
    SCOPED_TRACE(ustr);

    // Thursday 00:00, which none of the alarms below match while AIE is set
    EXPECT_TRUE(unit->writeDateTime(rtc_datetime_t(rtc_date_t(2026, 10, 22, 4), rtc_time_t(0, 0, 0))));
    hold_clock(true);

    // --- Day alarm ---
    {
        SCOPED_TRACE("Day alarm");
        EXPECT_TRUE(unit->writeAlarm(rtc_time_t(12, 30, -1), rtc_date_t(2000, 1, 15, 3)));  // date takes precedence

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, 12);
        EXPECT_EQ(rt.minutes, 30);
        EXPECT_EQ(rt.seconds, -1);
        EXPECT_EQ(rd.date, 15);
        EXPECT_EQ(rd.weekDay, -1);

        bool enabled{};
        EXPECT_TRUE(unit->readAlarmInterrupt(enabled));
        EXPECT_TRUE(enabled);
        uint8_t weekdays{0xFF};
        EXPECT_TRUE(unit->readAlarmWeekdays(weekdays));
        EXPECT_EQ(weekdays, 0);
    }

    // --- Week alarm ---
    {
        SCOPED_TRACE("Week alarm");
        EXPECT_TRUE(unit->writeAlarm(rtc_time_t(-1, 45, -1), rtc_date_t(2000, 1, -1, 5)));

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, -1);
        EXPECT_EQ(rt.minutes, 45);
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, 5);
        uint8_t weekdays{};
        EXPECT_TRUE(unit->readAlarmWeekdays(weekdays));
        EXPECT_EQ(weekdays, 0x20);
    }

    // --- Multiple weekdays ---
    {
        SCOPED_TRACE("Weekdays");
        EXPECT_TRUE(unit->writeAlarmWeekdays(rtc_time_t(7, 0, -1), 0x3E));  // Mon-Fri

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, 7);
        EXPECT_EQ(rt.minutes, 0);
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, -1);  // several days
        uint8_t weekdays{};
        EXPECT_TRUE(unit->readAlarmWeekdays(weekdays));
        EXPECT_EQ(weekdays, 0x3E);

        EXPECT_FALSE(unit->writeAlarmWeekdays(rtc_time_t(7, 0, -1), 0x80));
    }

    // --- All fields disabled ---
    {
        SCOPED_TRACE("Alarm all disabled");
        EXPECT_TRUE(unit->writeAlarm(rtc_time_t(-1, -1, -1), rtc_date_t(2000, 1, -1, -1)));

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, -1);
        EXPECT_EQ(rt.minutes, -1);
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, -1);

        bool enabled{true};
        EXPECT_TRUE(unit->readAlarmInterrupt(enabled));
        EXPECT_FALSE(enabled);
    }

    // --- Invalid values ---
    {
        SCOPED_TRACE("Invalid");
        EXPECT_FALSE(unit->writeAlarm(rtc_time_t(24, 0, -1), rtc_date_t(2000, 1, -1, -1)));
        EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 60, -1), rtc_date_t(2000, 1, -1, -1)));
        EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 0, -1), rtc_date_t(2000, 1, 32, -1)));
        EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 0, -1), rtc_date_t(2000, 1, -1, 7)));
    }

    // --- Alarm interrupt enable/disable ---
    {
        SCOPED_TRACE("AlarmInterrupt");
        EXPECT_TRUE(unit->clearAlarmFlag());
        EXPECT_TRUE(unit->writeAlarmInterrupt(true));
        bool enabled{};
        EXPECT_TRUE(unit->readAlarmInterrupt(enabled));
        EXPECT_TRUE(enabled);
        EXPECT_TRUE(unit->writeAlarmInterrupt(false));
        EXPECT_TRUE(unit->readAlarmInterrupt(enabled));
        EXPECT_FALSE(enabled);
    }
    hold_clock(false);

    // --- Alarm fires ---
    {
        SCOPED_TRACE("Alarm fires");
        EXPECT_TRUE(unit->writeDateTime(rtc_datetime_t(rtc_date_t(2026, 10, 22, 4), rtc_time_t(12, 0, 58))));
        EXPECT_TRUE(unit->writeAlarm(rtc_time_t(12, 1, -1), rtc_date_t(2000, 1, 22, -1)));
        mask_alarm_output();

        bool fired{true};
        EXPECT_TRUE(unit->readAlarmFlag(fired));
        EXPECT_FALSE(fired);
        EXPECT_TRUE(wait_flag([this](bool& f) { return unit->readAlarmFlag(f); }, 3500));

        EXPECT_TRUE(unit->clearAlarmFlag());
        EXPECT_TRUE(unit->readAlarmFlag(fired));
        EXPECT_FALSE(fired);
        EXPECT_TRUE(unit->writeAlarmInterrupt(false));
    }
}

// ============================================================
// Wakeup timer
// ============================================================

TEST_F(TestRX8130, Timer)
{
    SCOPED_TRACE(ustr);

    // --- Timer control with each TimerClock ---
    {
        TimerClock clocks[] = {TimerClock::Hz4096, TimerClock::Hz64, TimerClock::Hz1, TimerClock::HzPM,
                               TimerClock::HzPH};
        for (auto clk : clocks) {
            auto s = m5::utility::formatString("TimerClock %u", static_cast<unsigned>(clk));
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeTimerControl(false, clk));
            EXPECT_TRUE(unit->writeTimerValue(1000));
            EXPECT_TRUE(unit->writeTimerControl(true, clk));

            bool enabled{};
            TimerClock read_clk{};
            EXPECT_TRUE(unit->readTimerControl(enabled, read_clk));
            EXPECT_TRUE(enabled);
            EXPECT_EQ(read_clk, clk);
        }
        EXPECT_TRUE(unit->writeTimerControl(false, TimerClock::Hz1));
    }

    // --- Timer value round-trip (TE=0 reads the preset) ---
    {
        uint16_t values[] = {1, 0x1234, 0xFFFF};
        for (auto v : values) {
            auto s = m5::utility::formatString("TimerValue %u", v);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeTimerValue(v));
            uint16_t read_v{};
            EXPECT_TRUE(unit->readTimerValue(read_v));
            EXPECT_EQ(read_v, v);
        }
        EXPECT_FALSE(unit->writeTimerValue(0));

        // Preset cannot be written while running
        EXPECT_TRUE(unit->writeTimerControl(true, TimerClock::HzPH));
        EXPECT_FALSE(unit->writeTimerValue(100));
        EXPECT_TRUE(unit->writeTimerControl(false, TimerClock::HzPH));
    }

    // --- Timer interrupt / pause / count mode ---
    {
        SCOPED_TRACE("Control bits");
        bool b{};
        EXPECT_TRUE(unit->clearTimerFlag());  // the timer is stopped (TE = 0), so no event occurs while TIE is set
        EXPECT_TRUE(unit->writeTimerInterrupt(true));
        EXPECT_TRUE(unit->readTimerInterrupt(b));
        EXPECT_TRUE(b);
        EXPECT_TRUE(unit->writeTimerInterrupt(false));
        EXPECT_TRUE(unit->readTimerInterrupt(b));
        EXPECT_FALSE(b);

        EXPECT_TRUE(unit->writeTimerPause(true));
        EXPECT_TRUE(unit->readTimerPause(b));
        EXPECT_TRUE(b);
        EXPECT_TRUE(unit->writeTimerPause(false));
        EXPECT_TRUE(unit->readTimerPause(b));
        EXPECT_FALSE(b);

        TimerCountMode modes[] = {TimerCountMode::MainOnly, TimerCountMode::BackupOnly, TimerCountMode::Always};
        for (auto m : modes) {
            EXPECT_TRUE(unit->writeTimerCountMode(m));
            TimerCountMode rm{};
            EXPECT_TRUE(unit->readTimerCountMode(rm));
            EXPECT_EQ(rm, m);
        }
    }

    // --- Pause holds the counter ---
    {
        SCOPED_TRACE("Pause");
        EXPECT_TRUE(unit->writeTimerControl(false, TimerClock::Hz64));
        EXPECT_TRUE(unit->writeTimerValue(1000));
        EXPECT_TRUE(unit->writeTimerControl(true, TimerClock::Hz64));
        m5::utility::delay(200);
        EXPECT_TRUE(unit->writeTimerPause(true));
        uint16_t v0{}, v1{};
        EXPECT_TRUE(unit->readTimerValue(v0));
        EXPECT_LT(v0, 1000);
        m5::utility::delay(200);
        EXPECT_TRUE(unit->readTimerValue(v1));
        EXPECT_EQ(v0, v1);
        EXPECT_TRUE(unit->writeTimerPause(false));
        EXPECT_TRUE(unit->writeTimerControl(false, TimerClock::Hz64));
    }

    // --- writeTimer selects the clock ---
    {
        SCOPED_TRACE("writeTimer");
        // Settings only: keep the countdown paused so that no event occurs while TIE is set
        EXPECT_TRUE(unit->writeTimerCountMode(TimerCountMode::Always));  // TSTP is ignored when TBKE = 1
        EXPECT_TRUE(unit->writeTimerPause(true));
        hold_clock(true);
        struct {
            uint32_t msec;
            TimerClock clock;
        } table[] = {
            {1000, TimerClock::Hz64},       {60 * 1000, TimerClock::Hz64},     {2000 * 1000, TimerClock::Hz1},
            {7200 * 1000, TimerClock::Hz1}, {100000 * 1000, TimerClock::HzPM}, {10, TimerClock::Hz4096},
        };
        for (auto& e : table) {
            auto s = m5::utility::formatString("msec %u", e.msec);
            SCOPED_TRACE(s);
            auto actual = unit->writeTimer(e.msec, true);
            EXPECT_NEAR(static_cast<double>(actual), static_cast<double>(e.msec), e.msec / 256.0 + 1);

            bool enabled{}, ie{};
            TimerClock clk{};
            EXPECT_TRUE(unit->readTimerInterrupt(ie));
            EXPECT_TRUE(ie);
            mask_timer_output();  // 4096 Hz is not stopped by STOP; clear TIE as soon as it is checked
            EXPECT_TRUE(unit->readTimerControl(enabled, clk));
            EXPECT_TRUE(enabled);
            EXPECT_EQ(clk, e.clock);
        }

        EXPECT_EQ(unit->writeTimer(0), 0U);
        bool enabled{true}, ie{true};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_FALSE(enabled);
        EXPECT_TRUE(unit->readTimerInterrupt(ie));
        EXPECT_FALSE(ie);
        hold_clock(false);
        EXPECT_TRUE(unit->writeTimerPause(false));
    }

    // --- Timer fires (periodic) ---
    {
        SCOPED_TRACE("Periodic");
        EXPECT_EQ(start_timer(500, true), 500U);
        for (int i = 0; i < 3; ++i) {
            EXPECT_TRUE(wait_flag([this](bool& f) { return unit->readTimerFlag(f); }, 1000));
            EXPECT_TRUE(unit->clearTimerFlag());
        }
        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_TRUE(enabled);
        EXPECT_EQ(unit->writeTimer(0), 0U);
    }

    // --- Timer fires (one-shot, stopped by update) ---
    {
        SCOPED_TRACE("Oneshot");
        EXPECT_EQ(start_timer(500, false), 500U);
        auto timeout_at = m5::utility::millis() + 1500;
        bool enabled{true};
        TimerClock clk{};
        do {
            Units.update();
            m5::utility::delay(10);
            EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        } while (enabled && m5::utility::millis() <= timeout_at);
        EXPECT_FALSE(enabled);
        bool fired{true};
        EXPECT_TRUE(unit->readTimerFlag(fired));
        EXPECT_FALSE(fired);  // cleared by update
    }
}

// ============================================================
// Time update interrupt
// ============================================================

TEST_F(TestRX8130, Update)
{
    SCOPED_TRACE(ustr);

    UpdateTiming timings[] = {UpdateTiming::Minute, UpdateTiming::Second};
    for (auto ut : timings) {
        EXPECT_TRUE(unit->writeUpdateTiming(ut));
        UpdateTiming rut{};
        EXPECT_TRUE(unit->readUpdateTiming(rut));
        EXPECT_EQ(rut, ut);
    }

    // Toggle UIE just after an update event, so that the next one (1 s later) cannot occur while UIE is set
    bool b{};
    EXPECT_TRUE(unit->writeUpdateTiming(UpdateTiming::Second));
    EXPECT_TRUE(unit->clearUpdateFlag());
    EXPECT_TRUE(wait_flag([this](bool& f) { return unit->readUpdateFlag(f); }, 1500));
    EXPECT_TRUE(unit->clearUpdateFlag());
    hold_clock(true);
    EXPECT_TRUE(unit->writeUpdateInterrupt(true));
    EXPECT_TRUE(unit->readUpdateInterrupt(b));
    EXPECT_TRUE(b);
    EXPECT_TRUE(unit->writeUpdateInterrupt(false));
    EXPECT_TRUE(unit->readUpdateInterrupt(b));
    EXPECT_FALSE(b);
    hold_clock(false);

    // The update flag is set every second even with UIE=0
    EXPECT_TRUE(unit->clearUpdateFlag());
    EXPECT_TRUE(unit->readUpdateFlag(b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(wait_flag([this](bool& f) { return unit->readUpdateFlag(f); }, 1500));
    EXPECT_TRUE(unit->clearUpdateFlag());
}

// ============================================================
// Clock control
// ============================================================

TEST_F(TestRX8130, ClockControl)
{
    SCOPED_TRACE(ustr);

    // --- STOP holds the time ---
    {
        SCOPED_TRACE("Stop");
        EXPECT_TRUE(unit->writeTime(rtc_time_t(12, 0, 0)));
        EXPECT_TRUE(unit->writeStop(true));
        bool stopped{};
        EXPECT_TRUE(unit->readStop(stopped));
        EXPECT_TRUE(stopped);

        m5::utility::delay(2000);
        rtc_time_t rt{};
        EXPECT_TRUE(unit->readTime(rt));
        EXPECT_EQ(rt.seconds, 0);

        EXPECT_TRUE(unit->writeStop(false));
        EXPECT_TRUE(unit->readStop(stopped));
        EXPECT_FALSE(stopped);
    }

    // --- FOUT ---
    {
        SCOPED_TRACE("ClockOutput");
        ClockOutput outs[] = {ClockOutput::Hz32768, ClockOutput::Hz1024, ClockOutput::Hz1, ClockOutput::None};
        for (auto o : outs) {
            EXPECT_TRUE(unit->writeClockOutput(o));
            ClockOutput ro{};
            EXPECT_TRUE(unit->readClockOutput(ro));
            EXPECT_EQ(ro, o);
        }
    }

    // --- Digital offset ---
    {
        SCOPED_TRACE("DigitalOffset");
        bool org_en{};
        int8_t org_steps{};
        EXPECT_TRUE(unit->readDigitalOffset(org_en, org_steps));

        int8_t steps[] = {DIGITAL_OFFSET_MIN, -1, 0, 1, DIGITAL_OFFSET_MAX};
        for (auto st : steps) {
            auto s = m5::utility::formatString("Steps %d", st);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeDigitalOffset(true, st));
            bool en{};
            int8_t rst{};
            EXPECT_TRUE(unit->readDigitalOffset(en, rst));
            EXPECT_TRUE(en);
            EXPECT_EQ(rst, st);
        }
        EXPECT_FALSE(unit->writeDigitalOffset(true, -65));
        EXPECT_FALSE(unit->writeDigitalOffset(true, 64));

        // Datasheet examples: +192 -> L=63, -158 -> L=76 (-52), +11.57 -> 4, -11.57 -> L=124 (-4)
        struct {
            float ppm;
            int8_t steps;
        } table[] = {{192.0f, 63}, {-158.0f, -52}, {11.57f, 4}, {-11.57f, -4}, {-195.31f, -64}};
        for (auto& e : table) {
            auto s = m5::utility::formatString("PPM %f", e.ppm);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeDigitalOffsetPPM(e.ppm));
            bool en{};
            int8_t rst{};
            EXPECT_TRUE(unit->readDigitalOffset(en, rst));
            EXPECT_TRUE(en);
            EXPECT_EQ(rst, e.steps);
        }
        EXPECT_FALSE(unit->writeDigitalOffsetPPM(200.0f));
        EXPECT_TRUE(unit->writeDigitalOffsetPPM(0.0f));
        bool en{true};
        int8_t rst{};
        EXPECT_TRUE(unit->readDigitalOffset(en, rst));
        EXPECT_FALSE(en);

        EXPECT_TRUE(unit->writeDigitalOffset(org_en, org_steps));
    }
}

// ============================================================
// Status and backup power supply
// ============================================================

TEST_F(TestRX8130, Status)
{
    SCOPED_TRACE(ustr);

    // Writing the time clears VLF
    EXPECT_TRUE(unit->writeTime(rtc_time_t(12, 0, 0)));
    bool b{true};
    EXPECT_TRUE(unit->readVoltLow(b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(unit->clearVoltLow());

    // Values are hardware-dependent, just check that the reads succeed
    EXPECT_TRUE(unit->readBatteryLow(b));
    EXPECT_TRUE(unit->readBatteryFull(b));
    EXPECT_TRUE(unit->readResetFlag(b));
    EXPECT_TRUE(unit->clearResetFlag());
    EXPECT_TRUE(unit->readResetFlag(b));
    EXPECT_FALSE(b);
}

TEST_F(TestRX8130, Backup)
{
    SCOPED_TRACE(ustr);

    // begin() applies the config (default: switchover and charging on)
    bool sw{}, chg{};
    EXPECT_TRUE(unit->readPowerSwitch(sw));
    EXPECT_TRUE(sw);
    EXPECT_TRUE(unit->readBatteryCharge(chg));
    EXPECT_TRUE(chg);

    // Round-trips; restore the original values at the end
    FullChargeVoltage org_fv{};
    ResetVoltage org_rv{};
    DetectionTime org_dt{};
    bool org_lowdet{};
    EXPECT_TRUE(unit->readFullChargeVoltage(org_fv));
    EXPECT_TRUE(unit->readResetVoltage(org_rv));
    EXPECT_TRUE(unit->readDetectionTime(org_dt));
    EXPECT_TRUE(unit->readBatteryLowDetection(org_lowdet));

    FullChargeVoltage fvs[] = {FullChargeVoltage::V3_08, FullChargeVoltage::V2_92, FullChargeVoltage::V3_02};
    for (auto v : fvs) {
        EXPECT_TRUE(unit->writeFullChargeVoltage(v));
        FullChargeVoltage rv{};
        EXPECT_TRUE(unit->readFullChargeVoltage(rv));
        EXPECT_EQ(rv, v);
    }
    ResetVoltage rvs[] = {ResetVoltage::V2_70, ResetVoltage::V2_75};
    for (auto v : rvs) {
        EXPECT_TRUE(unit->writeResetVoltage(v));
        ResetVoltage rv{};
        EXPECT_TRUE(unit->readResetVoltage(rv));
        EXPECT_EQ(rv, v);
    }
    DetectionTime dts[] = {DetectionTime::Ms16, DetectionTime::Ms128, DetectionTime::Ms256, DetectionTime::Ms2};
    for (auto v : dts) {
        EXPECT_TRUE(unit->writeDetectionTime(v));
        DetectionTime rv{};
        EXPECT_TRUE(unit->readDetectionTime(rv));
        EXPECT_EQ(rv, v);
    }
    for (auto v : {true, false}) {
        EXPECT_TRUE(unit->writeBatteryLowDetection(v));
        bool rv{!v};
        EXPECT_TRUE(unit->readBatteryLowDetection(rv));
        EXPECT_EQ(rv, v);
    }
    // Other bits of CONTROL1 are kept
    EXPECT_TRUE(unit->readPowerSwitch(sw));
    EXPECT_TRUE(sw);
    EXPECT_TRUE(unit->readBatteryCharge(chg));
    EXPECT_TRUE(chg);

    EXPECT_TRUE(unit->writeFullChargeVoltage(org_fv));
    EXPECT_TRUE(unit->writeResetVoltage(org_rv));
    EXPECT_TRUE(unit->writeDetectionTime(org_dt));
    EXPECT_TRUE(unit->writeBatteryLowDetection(org_lowdet));
}

// ============================================================
// User RAM
// ============================================================

TEST_F(TestRX8130, UserRAM)
{
    SCOPED_TRACE(ustr);

    uint8_t org[4]{};
    EXPECT_TRUE(unit->readUserRAM(org, 0, 4));

    const uint8_t data[4] = {0x12, 0x34, 0x56, 0x78};
    EXPECT_TRUE(unit->writeUserRAM(data, 0, 4));
    uint8_t buf[4]{};
    EXPECT_TRUE(unit->readUserRAM(buf, 0, 4));
    EXPECT_EQ(memcmp(buf, data, 4), 0);

    const uint8_t v = 0xA5;
    EXPECT_TRUE(unit->writeUserRAM(&v, 3, 1));
    uint8_t rv{};
    EXPECT_TRUE(unit->readUserRAM(&rv, 3, 1));
    EXPECT_EQ(rv, 0xA5);
    EXPECT_TRUE(unit->readUserRAM(&rv, 2, 1));
    EXPECT_EQ(rv, 0x56);

    EXPECT_FALSE(unit->readUserRAM(buf, 1, 4));
    EXPECT_FALSE(unit->writeUserRAM(data, 4, 1));
    EXPECT_FALSE(unit->readUserRAM(buf, 0, 0));
    EXPECT_FALSE(unit->readUserRAM(nullptr, 0, 1));

    EXPECT_TRUE(unit->writeUserRAM(org, 0, 4));
}

// ============================================================
// M5Unified compatible API
// ============================================================

TEST_F(TestRX8130, CompatAPI)
{
    SCOPED_TRACE(ustr);

    // --- setDateTime / getDateTime ---
    {
        m5::rtc_datetime_t wdt{{2026, 10, 22, 4}, {8, 9, 10}};
        EXPECT_TRUE(unit->setDateTime(wdt));
        m5::rtc_datetime_t rdt{};
        EXPECT_TRUE(unit->getDateTime(&rdt));
        EXPECT_EQ(rdt.date.year, 2026);
        EXPECT_EQ(rdt.date.month, 10);
        EXPECT_EQ(rdt.date.date, 22);
        EXPECT_EQ(rdt.date.weekDay, 4);
        EXPECT_EQ(rdt.time.hours, 8);
        EXPECT_EQ(rdt.time.minutes, 9);

        auto d = unit->getDate();
        EXPECT_EQ(d.year, 2026);
        auto t = unit->getTime();
        EXPECT_EQ(t.hours, 8);

        EXPECT_TRUE(unit->setTime(m5::rtc_time_t{23, 0, 0}));
        EXPECT_EQ(unit->getTime().hours, 23);
        EXPECT_TRUE(unit->setDate(m5::rtc_date_t{2030, 1, 2, 3}));
        EXPECT_EQ(unit->getDate().year, 2030);
        EXPECT_FALSE(unit->getDateTime(nullptr));
    }

    // --- Alarm ---
    {
        hold_clock(true);
        m5::rtc_time_t at{6, 30, 0};
        EXPECT_EQ(unit->setAlarmIRQ(at), 1);
        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, 6);
        EXPECT_EQ(rt.minutes, 30);
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, -1);

        m5::rtc_date_t ad{2000, 1, -1, 2};
        EXPECT_EQ(unit->setAlarmIRQ(ad, at), 1);
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rd.weekDay, 2);

        m5::rtc_time_t none{-1, -1, -1};
        EXPECT_EQ(unit->setAlarmIRQ(none), 0);
        hold_clock(false);
    }

    // --- Timer and IRQ status ---
    {
        unit->clearIRQ();
        EXPECT_FALSE(unit->getIRQstatus());
        if (irq_output_allowed()) {
            EXPECT_EQ(unit->setTimerIRQ(500), 500U);
        } else {
            // setTimerIRQ() sets TIE; pause the countdown until TIE is cleared
            EXPECT_TRUE(unit->writeTimerCountMode(TimerCountMode::Always));
            EXPECT_TRUE(unit->writeTimerPause(true));
            EXPECT_EQ(unit->setTimerIRQ(500), 500U);
            EXPECT_TRUE(unit->writeTimerInterrupt(false));
            EXPECT_TRUE(unit->writeTimerPause(false));
        }
        EXPECT_TRUE(wait_flag([this](bool& f) { return unit->readTimerFlag(f); }, 1000));
        EXPECT_TRUE(unit->getIRQstatus());
        unit->clearIRQ();
        EXPECT_FALSE(unit->getIRQstatus());

        // The 2 s timer is stopped before it expires
        hold_clock(true);
        EXPECT_EQ(unit->setAlarmIRQ(2), 2);
        EXPECT_EQ(unit->setAlarmIRQ(-1), -1);
        hold_clock(false);

        unit->disableIRQ();
        bool b{true};
        EXPECT_TRUE(unit->readTimerInterrupt(b));
        EXPECT_FALSE(b);
        EXPECT_TRUE(unit->readAlarmInterrupt(b));
        EXPECT_FALSE(b);
        EXPECT_FALSE(unit->getIRQstatus());
    }

    // --- setSystemTimeFromRtc ---
    {
        EXPECT_TRUE(unit->setDateTime(m5::rtc_datetime_t{{2026, 10, 22, 4}, {10, 0, 0}}));
        unit->setSystemTimeFromRtc();
        time_t now  = time(nullptr);
        struct tm t = *localtime(&now);
        EXPECT_EQ(t.tm_year, 126);
        EXPECT_EQ(t.tm_mon, 9);
        EXPECT_EQ(t.tm_mday, 22);
        EXPECT_EQ(t.tm_hour, 10);
    }
}
