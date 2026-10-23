/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  UnitTest for UnitPCF8563
*/
#include <gtest/gtest.h>
#include <Wire.h>
#include <M5Unified.h>
#include <M5UnitUnified.hpp>
#include <googletest/test_template.hpp>
#include <googletest/test_helper.hpp>
#include <unit/unit_PCF8563.hpp>

using namespace m5::unit::googletest;
using namespace m5::unit;
using namespace m5::unit::pcf8563;

class TestPCF8563 : public I2CComponentTestBase<UnitPCF8563> {
protected:
    virtual UnitPCF8563* get_instance() override
    {
        return new m5::unit::UnitPCF8563();
    }

    // Keep the unit clock: save the date/time and restore it (plus the elapsed time) after each test.
    // Interrupts and STOP are reset before and after each test, so that a failed test does not affect the next
    virtual void SetUp() override
    {
        I2CComponentTestBase<UnitPCF8563>::SetUp();
        if (unit) {
            unit->disableIRQ();
            unit->writeStop(false);
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
        I2CComponentTestBase<UnitPCF8563>::TearDown();
    }

    bool _saved{};
    time_t _saved_at{};
    uint32_t _saved_ms{};
};

namespace {
// A second may pass between the write and the read
void expect_seconds(const int actual, const int written)
{
    EXPECT_TRUE(actual == written || actual == written + 1) << "seconds " << actual << " written " << written;
}
}  // namespace

// ============================================================
// DateTime
// ============================================================

TEST_F(TestPCF8563, DateTime)
{
    SCOPED_TRACE(ustr);

    // --- Time round-trip ---
    {
        rtc_time_t write_vals[] = {
            {0, 0, 0}, {12, 30, 45}, {23, 59, 58},  // a second later stays on the same day
        };
        for (auto& wt : write_vals) {
            auto s = m5::utility::formatString("Time %d:%d:%d", wt.hours, wt.minutes, wt.seconds);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeTime(wt));

            rtc_time_t rt{};
            EXPECT_TRUE(unit->readTime(rt));
            EXPECT_EQ(rt.hours, wt.hours);
            EXPECT_EQ(rt.minutes, wt.minutes);
            expect_seconds(rt.seconds, wt.seconds);
        }
    }

    // --- Date round-trip ---
    // 2000 or later
    {
        SCOPED_TRACE("Date 2026-02-26");
        rtc_date_t wd(2026, 2, 26, 4);
        EXPECT_TRUE(unit->writeDate(wd));

        rtc_date_t rd{};
        EXPECT_TRUE(unit->readDate(rd));
        EXPECT_EQ(rd.year, 2026);
        EXPECT_EQ(rd.month, 2);
        EXPECT_EQ(rd.date, 26);
        EXPECT_EQ(rd.weekDay, 4);
    }
    // 1900s (century bit)
    {
        SCOPED_TRACE("Date 1999-12-31");
        rtc_date_t wd(1999, 12, 31, 5);
        EXPECT_TRUE(unit->writeDate(wd));

        rtc_date_t rd{};
        EXPECT_TRUE(unit->readDate(rd));
        EXPECT_EQ(rd.year, 1999);
        EXPECT_EQ(rd.month, 12);
        EXPECT_EQ(rd.date, 31);
    }
    // Boundary: 2000-01-01
    {
        SCOPED_TRACE("Date 2000-01-01");
        rtc_date_t wd(2000, 1, 1, 6);
        EXPECT_TRUE(unit->writeDate(wd));

        rtc_date_t rd{};
        EXPECT_TRUE(unit->readDate(rd));
        EXPECT_EQ(rd.year, 2000);
        EXPECT_EQ(rd.month, 1);
        EXPECT_EQ(rd.date, 1);
    }

    // --- DateTime round-trip (rtc_datetime_t) ---
    {
        SCOPED_TRACE("DateTime rtc_datetime_t");
        rtc_datetime_t wdt(rtc_date_t(2026, 6, 15, 1), rtc_time_t(8, 30, 0));
        EXPECT_TRUE(unit->writeDateTime(wdt));

        rtc_datetime_t rdt{};
        EXPECT_TRUE(unit->readDateTime(rdt));
        EXPECT_EQ(rdt.date.year, 2026);
        EXPECT_EQ(rdt.date.month, 6);
        EXPECT_EQ(rdt.date.date, 15);
        EXPECT_EQ(rdt.time.hours, 8);
        EXPECT_EQ(rdt.time.minutes, 30);
        expect_seconds(rdt.time.seconds, 0);
    }

    // --- DateTime round-trip (struct tm) ---
    {
        SCOPED_TRACE("DateTime struct tm");
        struct tm wt {
        };
        wt.tm_year = 126;  // 2026
        wt.tm_mon  = 1;    // February
        wt.tm_mday = 26;
        wt.tm_wday = 4;
        wt.tm_hour = 14;
        wt.tm_min  = 20;
        wt.tm_sec  = 33;
        EXPECT_TRUE(unit->writeDateTime(wt));

        struct tm rt {
        };
        EXPECT_TRUE(unit->readDateTime(rt));
        EXPECT_EQ(rt.tm_year, 126);
        EXPECT_EQ(rt.tm_mon, 1);
        EXPECT_EQ(rt.tm_mday, 26);
        EXPECT_EQ(rt.tm_hour, 14);
        EXPECT_EQ(rt.tm_min, 20);
        expect_seconds(rt.tm_sec, 33);
    }
}

// ============================================================
// Alarm
// ============================================================

TEST_F(TestPCF8563, Alarm)
{
    SCOPED_TRACE(ustr);

    // --- All fields enabled ---
    {
        SCOPED_TRACE("Alarm all enabled");
        rtc_time_t at(12, 30, -1);  // seconds always -1
        rtc_date_t ad(2000, 1, 15, 3);
        EXPECT_TRUE(unit->writeAlarm(at, ad));

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.minutes, 30);
        EXPECT_EQ(rt.hours, 12);
        EXPECT_EQ(rt.seconds, -1);
        EXPECT_EQ(rd.date, 15);
        EXPECT_EQ(rd.weekDay, 3);
    }

    // --- Some fields disabled ---
    {
        SCOPED_TRACE("Alarm partial");
        rtc_time_t at(-1, 45, -1);
        rtc_date_t ad(2000, 1, -1, 5);
        EXPECT_TRUE(unit->writeAlarm(at, ad));

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, -1);
        EXPECT_EQ(rt.minutes, 45);
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, 5);
    }

    // --- All fields disabled ---
    {
        SCOPED_TRACE("Alarm all disabled");
        rtc_time_t at(-1, -1, -1);
        rtc_date_t ad(2000, 1, -1, -1);
        EXPECT_TRUE(unit->writeAlarm(at, ad));

        rtc_time_t rt{};
        rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.hours, -1);
        EXPECT_EQ(rt.minutes, -1);
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, -1);
    }

    // --- Alarm interrupt enable/disable ---
    {
        SCOPED_TRACE("AlarmInterrupt");
        EXPECT_TRUE(unit->writeAlarmInterrupt(true));

        bool enabled{};
        EXPECT_TRUE(unit->readAlarmInterrupt(enabled));
        EXPECT_TRUE(enabled);

        EXPECT_TRUE(unit->writeAlarmInterrupt(false));

        EXPECT_TRUE(unit->readAlarmInterrupt(enabled));
        EXPECT_FALSE(enabled);
    }

    // --- Clear alarm flag ---
    {
        SCOPED_TRACE("ClearAlarmFlag");
        EXPECT_TRUE(unit->clearAlarmFlag());

        bool fired{};
        EXPECT_TRUE(unit->readAlarmFlag(fired));
        EXPECT_FALSE(fired);
    }
}

// ============================================================
// Timer
// ============================================================

TEST_F(TestPCF8563, Timer)
{
    SCOPED_TRACE(ustr);

    // --- Timer control with each TimerClock ---
    {
        TimerClock clocks[] = {TimerClock::Hz4096, TimerClock::Hz64, TimerClock::Hz1, TimerClock::HzPM};
        for (auto clk : clocks) {
            auto s = m5::utility::formatString("TimerClock %u", static_cast<unsigned>(clk));
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeTimerControl(true, clk));

            bool enabled{};
            TimerClock read_clk{};
            EXPECT_TRUE(unit->readTimerControl(enabled, read_clk));
            EXPECT_TRUE(enabled);
            EXPECT_EQ(read_clk, clk);
        }
    }

    // --- Timer enable/disable ---
    {
        SCOPED_TRACE("TimerControl enabled=false");
        EXPECT_TRUE(unit->writeTimerControl(false, TimerClock::Hz1));

        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_FALSE(enabled);
        EXPECT_EQ(clk, TimerClock::Hz1);
    }

    // --- Timer value round-trip ---
    // BM8563/HYM8563 hardware behavior: register 0x0F is a live countdown
    // register. Both PCF8563 and HYM8563 datasheets state "when reading
    // the timer, the current countdown value is returned" and "it is not
    // possible to freeze the countdown timer counter during the read back."
    // In practice, reading back immediately after writing returns value-1
    // for values >= 2 when TE=1. M5Unified also never reads back timer
    // values after writing. Accept value or value-1.
    {
        EXPECT_TRUE(unit->writeTimerControl(true, TimerClock::HzPM));

        uint8_t values[] = {1, 100, 200, 255};
        for (auto v : values) {
            auto s = m5::utility::formatString("TimerValue %u", v);
            SCOPED_TRACE(s);
            EXPECT_TRUE(unit->writeTimerValue(v));

            uint8_t read_v{};
            EXPECT_TRUE(unit->readTimerValue(read_v));
            EXPECT_TRUE(read_v == v || read_v == static_cast<uint8_t>(v - 1))
                << "Expected " << (unsigned)v << " or " << (unsigned)(v - 1) << ", got " << (unsigned)read_v;
        }

        EXPECT_TRUE(unit->writeTimerControl(false, TimerClock::HzPM));
    }

    // --- Timer interrupt enable/disable ---
    {
        SCOPED_TRACE("TimerInterrupt");
        EXPECT_TRUE(unit->writeTimerInterrupt(true));

        bool enabled{};
        EXPECT_TRUE(unit->readTimerInterrupt(enabled));
        EXPECT_TRUE(enabled);

        EXPECT_TRUE(unit->writeTimerInterrupt(false));

        EXPECT_TRUE(unit->readTimerInterrupt(enabled));
        EXPECT_FALSE(enabled);
    }

    // --- Clear timer flag ---
    {
        SCOPED_TRACE("ClearTimerFlag");
        EXPECT_TRUE(unit->clearTimerFlag());

        bool fired{};
        EXPECT_TRUE(unit->readTimerFlag(fired));
        EXPECT_FALSE(fired);
    }
}

// ============================================================
// Clock Control
// ============================================================

TEST_F(TestPCF8563, ClockControl)
{
    SCOPED_TRACE(ustr);

    // --- STOP round-trip ---
    {
        SCOPED_TRACE("Stop");
        EXPECT_TRUE(unit->writeStop(true));

        bool stopped{};
        EXPECT_TRUE(unit->readStop(stopped));
        EXPECT_TRUE(stopped);

        EXPECT_TRUE(unit->writeStop(false));

        EXPECT_TRUE(unit->readStop(stopped));
        EXPECT_FALSE(stopped);
    }

    // --- STOP preserves time registers ---
    {
        SCOPED_TRACE("Stop preserves time");
        // Stop first, then write the time (the setting order recommended by the datasheet)
        EXPECT_TRUE(unit->writeStop(true));
        pcf8563::rtc_time_t wt(12, 0, 0);
        EXPECT_TRUE(unit->writeTime(wt));
        m5::utility::delay(2000);  // wait 2 seconds while stopped

        pcf8563::rtc_time_t rt{};
        EXPECT_TRUE(unit->readTime(rt));
        // Clock was stopped, so seconds should still be 0 (not 2)
        EXPECT_EQ(rt.seconds, 0);

        EXPECT_TRUE(unit->writeStop(false));
    }
}

// ============================================================
// Status
// ============================================================

TEST_F(TestPCF8563, Status)
{
    SCOPED_TRACE(ustr);

    bool low{};
    EXPECT_TRUE(unit->readVoltLow(low));
    // Value is hardware-dependent, just check that the read succeeds
}

// ============================================================
// M5Unified Compat API
// ============================================================

TEST_F(TestPCF8563, CompatAPI)
{
    SCOPED_TRACE(ustr);

    // --- setDateTime / getDateTime ---
    {
        SCOPED_TRACE("setDateTime/getDateTime");
        m5::rtc_datetime_t wdt;
        wdt.date = {2026, 2, 26, 4};
        wdt.time = {14, 20, 33};
        EXPECT_TRUE(unit->setDateTime(wdt));

        m5::rtc_datetime_t rdt;
        EXPECT_TRUE(unit->getDateTime(&rdt));
        EXPECT_EQ(rdt.date.year, 2026);
        EXPECT_EQ(rdt.date.month, 2);
        EXPECT_EQ(rdt.date.date, 26);
        EXPECT_EQ(rdt.time.hours, 14);
        EXPECT_EQ(rdt.time.minutes, 20);
        expect_seconds(rdt.time.seconds, 33);
    }

    // --- setTime / getTime ---
    {
        SCOPED_TRACE("setTime/getTime");
        m5::rtc_time_t wt = {8, 15, 0};
        EXPECT_TRUE(unit->setTime(wt));

        m5::rtc_time_t rt = unit->getTime();
        EXPECT_EQ(rt.hours, 8);
        EXPECT_EQ(rt.minutes, 15);
        expect_seconds(rt.seconds, 0);
    }

    // --- setDate / getDate ---
    {
        SCOPED_TRACE("setDate/getDate");
        m5::rtc_date_t wd = {2026, 6, 15, 1};
        EXPECT_TRUE(unit->setDate(wd));

        m5::rtc_date_t rd = unit->getDate();
        EXPECT_EQ(rd.year, 2026);
        EXPECT_EQ(rd.month, 6);
        EXPECT_EQ(rd.date, 15);
    }

    // --- setAlarmIRQ(date, time) ---
    {
        SCOPED_TRACE("setAlarmIRQ date+time");
        m5::rtc_date_t ad = {2000, 1, 15, 3};
        m5::rtc_time_t at = {12, 30, -1};
        int result        = unit->setAlarmIRQ(ad, at);
        EXPECT_EQ(result, 1);

        // Verify via readAlarm
        pcf8563::rtc_time_t rt{};
        pcf8563::rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.minutes, 30);
        EXPECT_EQ(rt.hours, 12);
        EXPECT_EQ(rd.date, 15);
        EXPECT_EQ(rd.weekDay, 3);
    }

    // --- setTimerIRQ ---
    {
        SCOPED_TRACE("setTimerIRQ");
        uint32_t actual = unit->setTimerIRQ(5000);  // 5 seconds
        EXPECT_GT(actual, 0U);

        // Verify timer is enabled
        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_TRUE(enabled);

        uint8_t count{};
        EXPECT_TRUE(unit->readTimerValue(count));
        EXPECT_GT(count, 0U);
    }

    // --- setTimerIRQ(0) disables timer ---
    {
        SCOPED_TRACE("setTimerIRQ disable");
        uint32_t actual = unit->setTimerIRQ(0);
        EXPECT_EQ(actual, 0U);

        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_FALSE(enabled);
    }

    // --- disableIRQ ---
    {
        SCOPED_TRACE("disableIRQ");
        // First enable something
        unit->setTimerIRQ(5000);
        unit->writeAlarmInterrupt(true);

        unit->disableIRQ();

        // Verify CONTROL2 bits are cleared
        bool alarm_ie{};
        EXPECT_TRUE(unit->readAlarmInterrupt(alarm_ie));
        EXPECT_FALSE(alarm_ie);

        bool timer_ie{};
        EXPECT_TRUE(unit->readTimerInterrupt(timer_ie));
        EXPECT_FALSE(timer_ie);
    }

    // --- getVoltLow ---
    {
        SCOPED_TRACE("getVoltLow");
        // Just verify it doesn't crash; value is hardware-dependent
        (void)unit->getVoltLow();
    }
}

// ============================================================
// M5Unified Compat Cross-API Consistency
// ============================================================

TEST_F(TestPCF8563, CompatCrossAPI)
{
    SCOPED_TRACE(ustr);

    // --- Write via M5UnitUnified, read via compat (DateTime) ---
    {
        SCOPED_TRACE("WriteUnified_ReadCompat DateTime");
        rtc_datetime_t wdt(rtc_date_t(2026, 3, 15, 0), rtc_time_t(10, 20, 30));
        EXPECT_TRUE(unit->writeDateTime(wdt));

        m5::rtc_datetime_t rdt;
        EXPECT_TRUE(unit->getDateTime(&rdt));
        EXPECT_EQ(rdt.date.year, 2026);
        EXPECT_EQ(rdt.date.month, 3);
        EXPECT_EQ(rdt.date.date, 15);
        EXPECT_EQ(rdt.time.hours, 10);
        EXPECT_EQ(rdt.time.minutes, 20);
        expect_seconds(rdt.time.seconds, 30);
    }

    // --- Write via compat, read via M5UnitUnified (DateTime) ---
    {
        SCOPED_TRACE("WriteCompat_ReadUnified DateTime");
        m5::rtc_datetime_t wdt;
        wdt.date = {2025, 12, 31, 3};
        wdt.time = {23, 59, 50};
        EXPECT_TRUE(unit->setDateTime(wdt));

        rtc_datetime_t rdt{};
        EXPECT_TRUE(unit->readDateTime(rdt));
        EXPECT_EQ(rdt.date.year, 2025);
        EXPECT_EQ(rdt.date.month, 12);
        EXPECT_EQ(rdt.date.date, 31);
        EXPECT_EQ(rdt.time.hours, 23);
        EXPECT_EQ(rdt.time.minutes, 59);
        expect_seconds(rdt.time.seconds, 50);
    }

    // --- Write via M5UnitUnified, read via compat (Time only) ---
    {
        SCOPED_TRACE("WriteUnified_ReadCompat Time");
        rtc_time_t wt(7, 45, 12);
        EXPECT_TRUE(unit->writeTime(wt));

        m5::rtc_time_t rt = unit->getTime();
        EXPECT_EQ(rt.hours, 7);
        EXPECT_EQ(rt.minutes, 45);
        expect_seconds(rt.seconds, 12);
    }

    // --- Write via compat, read via M5UnitUnified (Time only) ---
    {
        SCOPED_TRACE("WriteCompat_ReadUnified Time");
        m5::rtc_time_t wt = {18, 30, 55};
        EXPECT_TRUE(unit->setTime(wt));

        rtc_time_t rt{};
        EXPECT_TRUE(unit->readTime(rt));
        EXPECT_EQ(rt.hours, 18);
        EXPECT_EQ(rt.minutes, 30);
        expect_seconds(rt.seconds, 55);
    }

    // --- Write via M5UnitUnified, read via compat (Date only) ---
    {
        SCOPED_TRACE("WriteUnified_ReadCompat Date");
        rtc_date_t wd(2026, 7, 4, 6);
        EXPECT_TRUE(unit->writeDate(wd));

        m5::rtc_date_t rd = unit->getDate();
        EXPECT_EQ(rd.year, 2026);
        EXPECT_EQ(rd.month, 7);
        EXPECT_EQ(rd.date, 4);
    }

    // --- Write via compat, read via M5UnitUnified (Date only) ---
    {
        SCOPED_TRACE("WriteCompat_ReadUnified Date");
        m5::rtc_date_t wd = {2024, 2, 29, 4};
        EXPECT_TRUE(unit->setDate(wd));

        rtc_date_t rd{};
        EXPECT_TRUE(unit->readDate(rd));
        EXPECT_EQ(rd.year, 2024);
        EXPECT_EQ(rd.month, 2);
        EXPECT_EQ(rd.date, 29);
    }
}

// ============================================================
// M5Unified Compat Alarm/Timer
// ============================================================

TEST_F(TestPCF8563, CompatAlarmTimer)
{
    SCOPED_TRACE(ustr);

    // --- setAlarmIRQ(int afterSeconds) positive ---
    {
        SCOPED_TRACE("setAlarmIRQ(int) positive");
        int actual = unit->setAlarmIRQ(10);
        EXPECT_GT(actual, 0);

        // Timer should be enabled
        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_TRUE(enabled);
        EXPECT_EQ(clk, TimerClock::Hz1);

        bool tie{};
        EXPECT_TRUE(unit->readTimerInterrupt(tie));
        EXPECT_TRUE(tie);
    }

    // --- setAlarmIRQ(int) negative disables ---
    {
        SCOPED_TRACE("setAlarmIRQ(int) negative");
        int actual = unit->setAlarmIRQ(-1);
        EXPECT_EQ(actual, -1);

        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_FALSE(enabled);
    }

    // --- writeTimer vs setTimerIRQ equivalence ---
    {
        SCOPED_TRACE("writeTimer vs setTimerIRQ");

        // writeTimer
        uint32_t actual1 = unit->writeTimer(5000);

        bool en1{};
        TimerClock clk1{};
        EXPECT_TRUE(unit->readTimerControl(en1, clk1));
        uint8_t val1{};
        EXPECT_TRUE(unit->readTimerValue(val1));
        bool tie1{};
        EXPECT_TRUE(unit->readTimerInterrupt(tie1));

        // Reset
        unit->writeTimer(0);

        // setTimerIRQ (same milliseconds)
        uint32_t actual2 = unit->setTimerIRQ(5000);

        bool en2{};
        TimerClock clk2{};
        EXPECT_TRUE(unit->readTimerControl(en2, clk2));
        uint8_t val2{};
        EXPECT_TRUE(unit->readTimerValue(val2));
        bool tie2{};
        EXPECT_TRUE(unit->readTimerInterrupt(tie2));

        // Should produce identical results
        EXPECT_EQ(actual1, actual2);
        EXPECT_EQ(en1, en2);
        EXPECT_EQ(clk1, clk2);
        // Timer values may differ by 1 due to countdown, accept that
        EXPECT_TRUE(val1 == val2 || val1 == val2 + 1 || val1 + 1 == val2)
            << "val1=" << (unsigned)val1 << " val2=" << (unsigned)val2;
        EXPECT_EQ(tie1, tie2);

        unit->writeTimer(0);
    }

    // --- writeAlarm vs setAlarmIRQ equivalence ---
    {
        SCOPED_TRACE("writeAlarm vs setAlarmIRQ");

        // writeAlarm
        rtc_time_t at(14, 30, -1);
        rtc_date_t ad(2000, 1, 25, -1);
        EXPECT_TRUE(unit->writeAlarm(at, ad));

        rtc_time_t rt1{};
        rtc_date_t rd1{};
        EXPECT_TRUE(unit->readAlarm(rt1, rd1));
        bool aie1{};
        EXPECT_TRUE(unit->readAlarmInterrupt(aie1));

        // setAlarmIRQ (same values)
        m5::rtc_date_t cd = {2000, 1, 25, -1};
        m5::rtc_time_t ct = {14, 30, -1};
        int result        = unit->setAlarmIRQ(cd, ct);
        EXPECT_EQ(result, 1);

        rtc_time_t rt2{};
        rtc_date_t rd2{};
        EXPECT_TRUE(unit->readAlarm(rt2, rd2));
        bool aie2{};
        EXPECT_TRUE(unit->readAlarmInterrupt(aie2));

        // Should produce identical alarm register contents
        EXPECT_EQ(rt1.minutes, rt2.minutes);
        EXPECT_EQ(rt1.hours, rt2.hours);
        EXPECT_EQ(rd1.date, rd2.date);
        EXPECT_EQ(rd1.weekDay, rd2.weekDay);
        EXPECT_EQ(aie1, aie2);
    }

    // --- getIRQstatus ---
    {
        SCOPED_TRACE("getIRQstatus");
        // Clear all flags
        unit->clearAlarmFlag();
        unit->clearTimerFlag();

        EXPECT_FALSE(unit->getIRQstatus());
    }

    // --- clearIRQ ---
    {
        SCOPED_TRACE("clearIRQ");
        unit->clearIRQ();

        // After clearing, both AF and TF should be 0
        bool af{};
        EXPECT_TRUE(unit->readAlarmFlag(af));
        EXPECT_FALSE(af);
        bool tf{};
        EXPECT_TRUE(unit->readTimerFlag(tf));
        EXPECT_FALSE(tf);
    }
}

// ============================================================
// M5Unified Compat Nullptr Safety
// ============================================================

TEST_F(TestPCF8563, CompatNullptr)
{
    SCOPED_TRACE(ustr);

    // First set known values
    m5::rtc_datetime_t init;
    init.date = {2026, 1, 1, 4};
    init.time = {12, 0, 0};
    EXPECT_TRUE(unit->setDateTime(init));

    // --- getDateTime(nullptr) returns false ---
    {
        SCOPED_TRACE("getDateTime nullptr");
        EXPECT_FALSE(unit->getDateTime(static_cast<m5::rtc_datetime_t*>(nullptr)));
    }

    // --- getDateTime(date, nullptr) reads date only ---
    {
        SCOPED_TRACE("getDateTime date_only");
        m5::rtc_date_t rd;
        EXPECT_TRUE(unit->getDateTime(&rd, nullptr));
        EXPECT_EQ(rd.year, 2026);
        EXPECT_EQ(rd.month, 1);
        EXPECT_EQ(rd.date, 1);
    }

    // --- getDateTime(nullptr, time) reads time only ---
    {
        SCOPED_TRACE("getDateTime time_only");
        m5::rtc_time_t rt;
        EXPECT_TRUE(unit->getDateTime(nullptr, &rt));
        EXPECT_EQ(rt.hours, 12);
        EXPECT_EQ(rt.minutes, 0);
    }

    // --- setDateTime(nullptr) returns false ---
    {
        SCOPED_TRACE("setDateTime nullptr");
        EXPECT_FALSE(unit->setDateTime(static_cast<const m5::rtc_datetime_t*>(nullptr)));
    }

    // --- setDateTime(date, nullptr) writes date only ---
    {
        SCOPED_TRACE("setDateTime date_only");
        m5::rtc_date_t wd = {2025, 6, 15, 0};
        EXPECT_TRUE(unit->setDateTime(&wd, nullptr));

        // Date should be updated
        m5::rtc_date_t rd = unit->getDate();
        EXPECT_EQ(rd.year, 2025);
        EXPECT_EQ(rd.month, 6);
        EXPECT_EQ(rd.date, 15);

        // Time should be unchanged (still 12:00:xx)
        m5::rtc_time_t rt = unit->getTime();
        EXPECT_EQ(rt.hours, 12);
        EXPECT_EQ(rt.minutes, 0);
    }

    // --- setDateTime(nullptr, time) writes time only ---
    {
        SCOPED_TRACE("setDateTime time_only");
        m5::rtc_time_t wt = {8, 30, 45};
        EXPECT_TRUE(unit->setDateTime(nullptr, &wt));

        // Time should be updated
        m5::rtc_time_t rt = unit->getTime();
        EXPECT_EQ(rt.hours, 8);
        EXPECT_EQ(rt.minutes, 30);
        expect_seconds(rt.seconds, 45);

        // Date should be unchanged (still 2025-06-15)
        m5::rtc_date_t rd = unit->getDate();
        EXPECT_EQ(rd.year, 2025);
        EXPECT_EQ(rd.month, 6);
        EXPECT_EQ(rd.date, 15);
    }
}

// ============================================================
// Timer /INT output mode (TI_TP)
// ============================================================

TEST_F(TestPCF8563, TimerInterruptPulse)
{
    SCOPED_TRACE(ustr);

    for (auto pulse : {true, false}) {
        auto s = m5::utility::formatString("Pulse %d", pulse);
        SCOPED_TRACE(s);
        EXPECT_TRUE(unit->writeTimerInterruptPulse(pulse));
        bool rp{!pulse};
        EXPECT_TRUE(unit->readTimerInterruptPulse(rp));
        EXPECT_EQ(rp, pulse);

        // writeTimer keeps the /INT output mode
        EXPECT_GT(unit->writeTimer(3000), 0U);
        rp = !pulse;
        EXPECT_TRUE(unit->readTimerInterruptPulse(rp));
        EXPECT_EQ(rp, pulse);
        bool enabled{};
        TimerClock clk{};
        EXPECT_TRUE(unit->readTimerControl(enabled, clk));
        EXPECT_TRUE(enabled);
        EXPECT_EQ(unit->writeTimer(0), 0U);
    }
}

// ============================================================
// Compat: setAlarmIRQ time-only
// ============================================================

TEST_F(TestPCF8563, CompatAlarmTimeOnly)
{
    SCOPED_TRACE(ustr);

    // --- setAlarmIRQ(rtc_time_t) sets time-only alarm ---
    {
        SCOPED_TRACE("setAlarmIRQ time-only");
        m5::rtc_time_t at = {9, 15, -1};
        int result        = unit->setAlarmIRQ(at);
        EXPECT_EQ(result, 1);

        pcf8563::rtc_time_t rt{};
        pcf8563::rtc_date_t rd{};
        EXPECT_TRUE(unit->readAlarm(rt, rd));
        EXPECT_EQ(rt.minutes, 15);
        EXPECT_EQ(rt.hours, 9);
        // date/weekDay should be disabled
        EXPECT_EQ(rd.date, -1);
        EXPECT_EQ(rd.weekDay, -1);

        bool aie{};
        EXPECT_TRUE(unit->readAlarmInterrupt(aie));
        EXPECT_TRUE(aie);
    }

    // --- setAlarmIRQ(rtc_time_t) all disabled ---
    {
        SCOPED_TRACE("setAlarmIRQ time-only all disabled");
        m5::rtc_time_t at = {-1, -1, -1};
        int result        = unit->setAlarmIRQ(at);
        EXPECT_EQ(result, 0);

        bool aie{};
        EXPECT_TRUE(unit->readAlarmInterrupt(aie));
        EXPECT_FALSE(aie);
    }
}

// ============================================================
// Compat: setSystemTimeFromRtc
// ============================================================

TEST_F(TestPCF8563, CompatSetSystemTime)
{
    SCOPED_TRACE(ustr);

    // Write a known datetime to RTC
    pcf8563::rtc_datetime_t wdt(pcf8563::rtc_date_t(2026, 3, 6, 5), pcf8563::rtc_time_t(12, 0, 0));
    EXPECT_TRUE(unit->writeDateTime(wdt));

    // Set system time from RTC
    unit->setSystemTimeFromRtc(nullptr);

    // Verify system time is close to what we wrote
    struct timeval tv {
    };
    gettimeofday(&tv, nullptr);
    struct tm* t = localtime(&tv.tv_sec);  // setSystemTimeFromRtc() converts with mktime (local time)

    EXPECT_EQ(t->tm_year + 1900, 2026);
    EXPECT_EQ(t->tm_mon + 1, 3);
    EXPECT_EQ(t->tm_mday, 6);
    EXPECT_EQ(t->tm_hour, 12);
    EXPECT_EQ(t->tm_min, 0);
}

// ============================================================
// The timer repeats (the PCF8563 has no one-shot mode) until writeTimer(0)
// ============================================================

TEST_F(TestPCF8563, TimerRepeats)
{
    SCOPED_TRACE(ustr);

    // Without a callback, update() leaves TF for readTimerFlag()
    EXPECT_EQ(unit->writeTimer(1000), 1000U);
    bool fired{};
    for (int i = 0; i < 2; ++i) {
        auto s = m5::utility::formatString("Count %d", i);
        SCOPED_TRACE(s);
        const auto start_at = m5::utility::millis();
        do {
            Units.update();
            m5::utility::delay(10);
            EXPECT_TRUE(unit->readTimerFlag(fired));
        } while (!fired && !m5::utility::hasElapsed(start_at, 2500));
        EXPECT_TRUE(fired);
        EXPECT_TRUE(unit->clearTimerFlag());
    }
    bool enabled{};
    TimerClock clk{};
    EXPECT_TRUE(unit->readTimerControl(enabled, clk));
    EXPECT_TRUE(enabled);

    EXPECT_EQ(unit->writeTimer(0), 0U);
    EXPECT_TRUE(unit->readTimerControl(enabled, clk));
    EXPECT_FALSE(enabled);
    bool ie{true};
    EXPECT_TRUE(unit->readTimerInterrupt(ie));
    EXPECT_FALSE(ie);
}

// ============================================================
// Invalid values are rejected; a negative weekday is calculated
// ============================================================

TEST_F(TestPCF8563, InvalidValues)
{
    SCOPED_TRACE(ustr);

    EXPECT_FALSE(unit->writeTime(rtc_time_t(24, 0, 0)));
    EXPECT_FALSE(unit->writeTime(rtc_time_t(0, 60, 0)));
    EXPECT_FALSE(unit->writeTime(rtc_time_t(0, 0, 60)));
    EXPECT_FALSE(unit->writeTime(rtc_time_t(-1, 0, 0)));
    EXPECT_FALSE(unit->writeDate(rtc_date_t(1899, 12, 31, 0)));
    EXPECT_FALSE(unit->writeDate(rtc_date_t(2100, 1, 1, 5)));
    EXPECT_FALSE(unit->writeDate(rtc_date_t(2026, 13, 1, 0)));
    EXPECT_FALSE(unit->writeDate(rtc_date_t(2026, 1, 0, 0)));
    EXPECT_FALSE(unit->writeDate(rtc_date_t(2026, 1, 1, 7)));

    EXPECT_FALSE(unit->writeAlarm(rtc_time_t(24, 0, -1), rtc_date_t(2000, 1, -1, -1)));
    EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 60, -1), rtc_date_t(2000, 1, -1, -1)));
    EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 0, -1), rtc_date_t(2000, 1, 0, -1)));
    EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 0, -1), rtc_date_t(2000, 1, 32, -1)));
    EXPECT_FALSE(unit->writeAlarm(rtc_time_t(0, 0, -1), rtc_date_t(2000, 1, -1, 7)));

    // Weekday is calculated when negative
    EXPECT_TRUE(unit->writeDate(rtc_date_t(2026, 10, 22, -1)));  // Thursday
    rtc_date_t rd{};
    EXPECT_TRUE(unit->readDate(rd));
    EXPECT_EQ(rd.weekDay, 4);
    EXPECT_TRUE(unit->writeDate(rtc_date_t(1999, 12, 31, -1)));  // Friday
    EXPECT_TRUE(unit->readDate(rd));
    EXPECT_EQ(rd.year, 1999);
    EXPECT_EQ(rd.weekDay, 5);
    EXPECT_TRUE(unit->writeDate(rtc_date_t(2000, 2, 29, -1)));  // Tuesday (January / February branch)
    EXPECT_TRUE(unit->readDate(rd));
    EXPECT_EQ(rd.weekDay, 2);
}

// ============================================================
// Writing CONTROL2 keeps the flag that is not cleared, and does not set it
// (on the Unit RTC writing 1 to AF/TF sets the flag, unlike the datasheets)
// ============================================================

TEST_F(TestPCF8563, FlagsKept)
{
    SCOPED_TRACE(ustr);

    // Let the alarm fire (AF)
    EXPECT_TRUE(unit->writeDateTime(rtc_datetime_t(rtc_date_t(2026, 10, 22, 4), rtc_time_t(12, 0, 58))));
    EXPECT_TRUE(unit->writeAlarm(rtc_time_t(12, 1, -1), rtc_date_t(2000, 1, -1, -1)));
    bool af{};
    auto start_at = m5::utility::millis();
    do {
        m5::utility::delay(10);
        EXPECT_TRUE(unit->readAlarmFlag(af));
    } while (!af && !m5::utility::hasElapsed(start_at, 3500));
    EXPECT_TRUE(af);

    // Clearing TF and changing the other CONTROL2 bits keep AF
    EXPECT_TRUE(unit->clearTimerFlag());
    EXPECT_TRUE(unit->writeTimerInterrupt(false));
    EXPECT_EQ(unit->writeTimer(0), 0U);
    EXPECT_TRUE(unit->readAlarmFlag(af));
    EXPECT_TRUE(af);

    EXPECT_TRUE(unit->clearAlarmFlag());
    EXPECT_TRUE(unit->readAlarmFlag(af));
    EXPECT_FALSE(af);
    EXPECT_TRUE(unit->writeAlarmInterrupt(false));

    // Clearing one flag does not set the other
    bool tf{true};
    EXPECT_TRUE(unit->readTimerFlag(tf));
    EXPECT_FALSE(tf);
    EXPECT_TRUE(unit->clearTimerFlag());
    EXPECT_TRUE(unit->readAlarmFlag(af));
    EXPECT_FALSE(af);
}

// ============================================================
// update() clears only the flags that have a callback
// ============================================================

TEST_F(TestPCF8563, UpdateClearsHandledFlags)
{
    SCOPED_TRACE(ustr);

    // Polling with on_alarm only: TF is left for readTimerFlag()
    auto cfg     = unit->config();
    cfg.polling  = true;
    cfg.on_alarm = []() {};
    cfg.on_timer = nullptr;
    unit->config(cfg);
    EXPECT_TRUE(unit->begin());  // config_t is applied by begin()

    auto wait_tf = [this]() {
        bool f{};
        const auto start_at = m5::utility::millis();
        do {
            m5::utility::delay(10);
            EXPECT_TRUE(unit->readTimerFlag(f));
        } while (!f && !m5::utility::hasElapsed(start_at, 2500));
        return f;
    };

    EXPECT_EQ(unit->writeTimer(1000), 1000U);
    EXPECT_TRUE(wait_tf());
    unit->update(true);
    bool fired{};
    EXPECT_TRUE(unit->readTimerFlag(fired));
    EXPECT_TRUE(fired);  // no on_timer: kept
    EXPECT_EQ(unit->writeTimer(0), 0U);

    // With on_timer, update() clears TF
    bool called{};
    static bool* called_ptr{};
    called_ptr   = &called;
    cfg.on_timer = []() { *called_ptr = true; };
    unit->config(cfg);
    EXPECT_TRUE(unit->begin());
    EXPECT_EQ(unit->writeTimer(1000), 1000U);
    EXPECT_TRUE(wait_tf());
    unit->update(true);
    EXPECT_TRUE(called);
    EXPECT_TRUE(unit->readTimerFlag(fired));
    EXPECT_FALSE(fired);
    EXPECT_EQ(unit->writeTimer(0), 0U);

    cfg.on_alarm = nullptr;
    cfg.on_timer = nullptr;
    unit->config(cfg);
    EXPECT_TRUE(unit->begin());
}

// ============================================================
// The clock runs, and the date and the century carry over
// ============================================================

TEST_F(TestPCF8563, Running)
{
    SCOPED_TRACE(ustr);

    // --- Clock runs ---
    {
        SCOPED_TRACE("Running");
        EXPECT_TRUE(unit->writeTime(rtc_time_t(12, 0, 0)));
        m5::utility::delay(2100);
        rtc_time_t rt{};
        EXPECT_TRUE(unit->readTime(rt));
        EXPECT_TRUE(rt.seconds == 2 || rt.seconds == 3) << "seconds " << (int)rt.seconds;
    }

    // --- Carry over to the next day, month, year and century (C bit toggles) ---
    {
        SCOPED_TRACE("Carry");
        EXPECT_TRUE(unit->writeDateTime(rtc_datetime_t(rtc_date_t(1999, 12, 31, 5), rtc_time_t(23, 59, 59))));
        m5::utility::delay(1100);
        rtc_datetime_t rdt{};
        EXPECT_TRUE(unit->readDateTime(rdt));
        EXPECT_EQ(rdt.date.year, 2000);
        EXPECT_EQ(rdt.date.month, 1);
        EXPECT_EQ(rdt.date.date, 1);
        EXPECT_EQ(rdt.date.weekDay, 6);
        EXPECT_EQ(rdt.time.hours, 0);
        EXPECT_EQ(rdt.time.minutes, 0);
    }
}

// ============================================================
// begin() keeps the flags (the wakeup source) and disables the interrupts
// ============================================================

TEST_F(TestPCF8563, BeginKeepsFlags)
{
    SCOPED_TRACE(ustr);

    EXPECT_EQ(unit->writeTimer(1000), 1000U);
    bool tf{};
    const auto start_at = m5::utility::millis();
    do {
        m5::utility::delay(10);
        EXPECT_TRUE(unit->readTimerFlag(tf));
    } while (!tf && !m5::utility::hasElapsed(start_at, 2500));
    EXPECT_TRUE(tf);

    EXPECT_TRUE(unit->begin());
    EXPECT_TRUE(unit->readTimerFlag(tf));
    EXPECT_TRUE(tf);  // kept
    bool ie{true}, enabled{true};
    TimerClock clk{};
    EXPECT_TRUE(unit->readTimerInterrupt(ie));
    EXPECT_FALSE(ie);
    EXPECT_TRUE(unit->readTimerControl(enabled, clk));
    EXPECT_FALSE(enabled);
    EXPECT_TRUE(unit->clearTimerFlag());
}
