/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  Example using M5UnitUnified for UnitRTC (PCF8563/BM8563/HYM8563)
  - Countdown timer using writeTimer() convenience API
    The timer repeats in hardware; the one-shot modes stop it with writeTimer(0) when it fires
  - BtnA click: cycle interval and repeat mode
    5s repeat -> 10s repeat -> 30s repeat -> 5s oneshot -> 10s oneshot -> stop -> ...
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

volatile uint32_t timer_count{};
volatile bool timer_fired{};
bool indicator_lit{};

// Mode table: {seconds, repeat}
struct TimerMode {
    uint8_t seconds;
    bool repeat;
};
constexpr TimerMode MODES[] = {
    {5, true}, {10, true}, {30, true}, {5, false}, {10, false}, {0, false},
};
size_t mode_idx{};

void on_timer()
{
    timer_count = timer_count + 1;
    timer_fired = true;
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

// Start timer with current mode
void start_timer()
{
    auto& m = MODES[mode_idx];

    if (m.seconds == 0) {
        unit.writeTimer(0);
        M5.Log.printf("Timer stopped\n");
        return;
    }

    uint32_t actual = unit.writeTimer(static_cast<uint32_t>(m.seconds) * 1000);
    M5.Log.printf("Timer: %lu ms, %s\n", (unsigned long)actual, m.repeat ? "repeat" : "oneshot");
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

    // Configure timer callback (polling mode)
    auto cfg     = unit.config();
    cfg.on_timer = on_timer;
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

    // Start with first mode
    start_timer();
}

void loop()
{
    M5.update();
    Units.update();

    static time_t prev{};
    time_t now = time(nullptr);

    // Timer fired: light up indicator + beep
    if (timer_fired) {
        timer_fired   = false;
        indicator_lit = true;
        M5.Log.printf("Timer fired! count=%lu\n", (unsigned long)timer_count);
        M5.Speaker.tone(2000, 20);
        // The PCF8563 has no one-shot timer; stop it here for the one-shot modes
        if (!MODES[mode_idx].repeat) {
            unit.writeTimer(0);
        }
        prev = 0;  // force redraw
    }

    // BtnA click: cycle mode
    if (M5.BtnA.wasClicked()) {
        mode_idx    = (mode_idx + 1) % m5::stl::size(MODES);
        timer_count = 0;
        start_timer();
        prev = 0;  // force redraw
    }

    // Update display every second
    if (now != prev) {
        struct tm local = *localtime(&now);

        auto& m        = MODES[mode_idx];
        uint32_t count = timer_count;

        // Serial
        M5.Log.printf("%04d-%02d-%02d(%s) %02d:%02d:%02d  Timer:%us %s Count:%lu\n", local.tm_year + 1900,
                      local.tm_mon + 1, local.tm_mday, WDAY_NAMES[local.tm_wday], local.tm_hour, local.tm_min,
                      local.tm_sec, m.seconds, m.repeat ? "repeat" : "oneshot", (unsigned long)count);

        // Draw to canvas (off-screen), then push to display
        canvas.fillScreen(TFT_BLACK);
        canvas.setCursor(0, 0);
        canvas_printf("%04d-%02d-%02d(%s)\n%02d:%02d:%02d\n\n", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      WDAY_NAMES[local.tm_wday], local.tm_hour, local.tm_min, local.tm_sec);
        if (m.seconds == 0) {
            canvas_printf("Timer: OFF\nCount: %lu\n\n[BtnA] Start\n", (unsigned long)count);
        } else {
            canvas_printf("Timer: %u sec\nMode:  %s\nCount: %lu\n\n[BtnA] Change\n", m.seconds,
                          m.repeat ? "repeat" : "oneshot", (unsigned long)count);
        }
        draw_indicator(indicator_lit);
        indicator_lit = false;
        canvas.pushSprite(0, 0);
        prev = now;
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
