/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  Example using M5UnitUnified for UnitRTC (PCF8563/BM8563/HYM8563)
  - Set alarm to 1 minute ahead, show countdown, notify when alarm fires
  - BtnA click: re-set alarm to +1 minute from current RTC time
*/
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedRTC.h>
#include <wiring/m5_unit_unified_wiring.hpp>
#include <sys/time.h>
#include <cstdint>

namespace {

// POSIX timezone string (default: JST-9 = UTC+9, no DST)
const char* YOUR_TIMEZONE = "JST-9";
// const char* YOUR_TIMEZONE = "EST5EDT,M3.2.0,M11.1.0";  // US Eastern (UTC-5, DST)
// const char* YOUR_TIMEZONE = "CST6CDT,M3.2.0,M11.1.0";  // US Central (UTC-6, DST)
// const char* YOUR_TIMEZONE = "PST8PDT,M3.2.0,M11.1.0";  // US Pacific (UTC-8, DST)
// const char* YOUR_TIMEZONE = "GMT0BST,M3.5.0/1,M10.5.0/2";    // UK (UTC+0, DST)
// const char* YOUR_TIMEZONE = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // EU Central (UTC+1, DST)
// const char* YOUR_TIMEZONE = "CST-8";  // China (UTC+8, no DST)
// See: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

constexpr char WDAY_NAMES[][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

auto& lcd = M5.Display;
LGFX_Sprite canvas(&lcd);
m5::unit::UnitUnified Units;
m5::unit::UnitRTC unit;

volatile bool alarm_fired{};
bool indicator_lit{};
int8_t cached_alarm_h{-1}, cached_alarm_m{-1};

// Convert GMT hour:minute to local time using current TZ offset
void gmt_to_local_hm(int8_t gmt_h, int8_t gmt_m, int8_t& local_h, int8_t& local_m)
{
    time_t now      = time(nullptr);
    struct tm local = *localtime(&now);
    struct tm gmt   = *gmtime(&now);
    int offset      = (local.tm_hour * 60 + local.tm_min) - (gmt.tm_hour * 60 + gmt.tm_min);
    if (offset > 720) {
        offset -= 1440;
    }
    if (offset < -720) {
        offset += 1440;
    }
    int total = gmt_h * 60 + gmt_m + offset;
    if (total >= 1440) {
        total -= 1440;
    }
    if (total < 0) {
        total += 1440;
    }
    local_h = total / 60;
    local_m = total % 60;
}

void on_alarm()
{
    alarm_fired = true;
}

void canvas_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void canvas_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    canvas.vprintf(fmt, args);
    va_end(args);
}

// Draw indicator circle at top-right corner of canvas
void draw_indicator(bool lit)
{
    int32_t r = canvas.height() / 20;
    if (r < 4) {
        r = 4;
    }
    int32_t x = canvas.width() - r - 4;
    int32_t y = r + 4;
    canvas.fillCircle(x, y, r, lit ? 2 : 3);  // palette: 2=TFT_BLUE, 3=TFT_DARKGREY
}

// Set alarm to current RTC time + 1 minute
bool set_alarm_plus1()
{
    using namespace m5::unit::pcf8563;

    rtc_time_t now_t{};
    rtc_date_t now_d{};
    if (!unit.readTime(now_t) || !unit.readDate(now_d)) {
        M5_LOGE("Failed to read current time");
        return false;
    }

    int8_t alarm_min  = now_t.minutes + 1;
    int8_t alarm_hour = now_t.hours;
    if (alarm_min >= 60) {
        alarm_min -= 60;
        alarm_hour++;
        if (alarm_hour >= 24) {
            alarm_hour = 0;
        }
    }

    // Set alarm (minutes + hours enabled, date/weekday disabled)
    // writeAlarm clears flag and enables interrupt automatically
    rtc_time_t at{alarm_hour, alarm_min, -1};
    rtc_date_t ad{2000, 1, -1, -1};

    if (!unit.writeAlarm(at, ad)) {
        return false;
    }

    // Cache alarm in local time for display
    gmt_to_local_hm(alarm_hour, alarm_min, cached_alarm_h, cached_alarm_m);
    time_t now      = time(nullptr);
    struct tm local = *localtime(&now);
    M5.Log.printf("Alarm set to %02d:%02d (current %02d:%02d:%02d)\n", cached_alarm_h, cached_alarm_m, local.tm_hour,
                  local.tm_min, local.tm_sec);
    return true;
}

}  // namespace

void setup()
{
    M5.begin();
    M5.setTouchButtonHeightByRatio(100);

    // The screen shall be in landscape mode
    if (lcd.height() > lcd.width()) {
        lcd.setRotation(1);
    }

    // Configure alarm callback (polling mode)
    auto cfg     = unit.config();
    cfg.on_alarm = on_alarm;
    unit.config(cfg);

    // Board-aware connection: NessoN1 -> SoftwareI2C on the GROVE port, NanoC6/NanoH2 -> M5.Ex_I2C, others -> Wire
    bool unit_ready = m5::unit::wiring::addI2C(Units, unit) && Units.begin();
    if (!unit_ready) {
        M5_LOGE("Failed to begin");
        m5::unit::wiring::failStop();
    }
    M5_LOGI("M5UnitUnified initialized");
    M5_LOGI("%s", Units.debugInfo().c_str());

    // Check voltage low (battery backup lost)
    if (unit.getVoltLow()) {
        M5_LOGW("RTC voltage low - time may be invalid");
    }

    // Sync system clock from RTC (stored as GMT) and set timezone
    {
        m5::unit::pcf8563::rtc_datetime_t dt{};
        if (unit.readDateTime(dt)) {
            struct tm gmt = dt.to_tm();
            time_t t      = mktime(&gmt);  // TZ is not set yet, so this is UTC
            struct timeval tv {
                t, 0
            };
            settimeofday(&tv, nullptr);
        } else {
            M5_LOGW("Failed to read the RTC, the system clock is not set");
        }
        setenv("TZ", YOUR_TIMEZONE, 1);
        tzset();
        M5_LOGI("System clock set from RTC (GMT), TZ=%s", YOUR_TIMEZONE);
    }

    // Create sprite for flicker-free drawing (2-bit palette, internal RAM)
    canvas.setPsram(false);
    canvas.setColorDepth(2);
    int32_t sw = lcd.width() < 320 ? lcd.width() : 320;
    int32_t sh = lcd.height() < 240 ? lcd.height() : 240;
    canvas.createSprite(sw, sh);
    canvas.setFont(lcd.getFont());
    canvas.setTextSize(lcd.getTextSizeX(), lcd.getTextSizeY());
    canvas.setPaletteColor(0, TFT_BLACK);
    canvas.setPaletteColor(1, TFT_WHITE);
    canvas.setPaletteColor(2, TFT_BLUE);
    canvas.setPaletteColor(3, TFT_DARKGREY);

    // Set alarm to +1 minute
    set_alarm_plus1();
}

void loop()
{
    M5.update();
    Units.update();

    static time_t prev{};
    time_t now = time(nullptr);

    // Handle alarm notification
    if (alarm_fired) {
        alarm_fired   = false;
        indicator_lit = true;
        M5.Log.printf("*** ALARM FIRED! ***\n");
        M5.Speaker.tone(2000, 20);

        // Re-set alarm to +1 minute
        set_alarm_plus1();
        prev = 0;  // force redraw
    }

    // Update display every second
    if (now != prev) {
        struct tm local = *localtime(&now);

        // Serial
        M5.Log.printf("%04d-%02d-%02d(%s) %02d:%02d:%02d  Alarm:%02d:%02d\n", local.tm_year + 1900, local.tm_mon + 1,
                      local.tm_mday, WDAY_NAMES[local.tm_wday], local.tm_hour, local.tm_min, local.tm_sec,
                      cached_alarm_h, cached_alarm_m);

        // Draw to canvas (off-screen), then push to display
        canvas.fillScreen(TFT_BLACK);
        canvas.setCursor(0, 0);
        canvas_printf("%04d-%02d-%02d(%s)\n%02d:%02d:%02d\n\nAlarm: %02d:%02d\n", local.tm_year + 1900,
                      local.tm_mon + 1, local.tm_mday, WDAY_NAMES[local.tm_wday], local.tm_hour, local.tm_min,
                      local.tm_sec, cached_alarm_h, cached_alarm_m);
        draw_indicator(indicator_lit);
        indicator_lit = false;
        canvas.pushSprite(0, 0);
        prev = now;
    }

    // BtnA click: re-set alarm to +1 minute
    if (M5.BtnA.wasClicked()) {
        M5.Log.printf("Re-setting alarm...\n");
        set_alarm_plus1();
        prev = 0;  // force redraw
    }
}

#if !defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#if CONFIG_FREERTOS_UNICORE
// Single-core SoCs: run loop() back-to-back, but every ~2 s yield a 5 ms slice so the IDLE task
// runs and feeds the task watchdog (default 5 s).
static inline void feedIdleTaskPeriodically(void)
{
    constexpr uint32_t FEED_INTERVAL_MS{2000};
    constexpr TickType_t FEED_SLEEP_TICKS{pdMS_TO_TICKS(5)};
    static uint32_t s_last_feed_ms{};
    const uint32_t now_ms{static_cast<uint32_t>(esp_timer_get_time() / 1000)};
    if (now_ms - s_last_feed_ms >= FEED_INTERVAL_MS) {
        s_last_feed_ms = now_ms;
        vTaskDelay(FEED_SLEEP_TICKS);
    }
}
#endif

extern "C" void app_main(void)
{
    setup();
    for (;;) {
#if CONFIG_FREERTOS_UNICORE
        feedIdleTaskPeriodically();
#endif
        loop();
    }
}
#endif
