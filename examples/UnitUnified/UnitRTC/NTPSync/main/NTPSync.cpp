/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  Example using M5UnitUnified for UnitRTC (PCF8563/BM8563/HYM8563) or the built-in RX8130CE of Tab5
  - Sync time from NTP via Wi-Fi, then set RTC
  Wi-Fi uses esp_wifi directly (wifi_connect.hpp), so the same code builds with Arduino and ESP-IDF.
  ESP32-H2 (NanoH2) has no Wi-Fi and is not supported by this example.
*/
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedRTC.h>
#include <wiring/m5_unit_unified_wiring.hpp>
#include "wifi_connect.hpp"

// *************************************************************
// Choose one define symbol to match the RTC you are using
// *************************************************************
#if !defined(USING_UNIT_RTC) && !defined(BUILTIN_UNIT_RX8130)
// For Unit RTC (U126)
#define USING_UNIT_RTC
// For the built-in RX8130CE of Tab5 (internal I2C)
// #define BUILTIN_UNIT_RX8130
#endif
#include <esp_idf_version.h>
#include <esp_random.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include <esp_netif_sntp.h>
#else
#include <esp_sntp.h>
#endif
#include <sys/time.h>
#include <algorithm>  // std::shuffle
#include <cstdint>
#include <cstdlib>

namespace {
// ==============================================================
// NOTICE: Set your Wi-Fi credentials and timezone before building
// ==============================================================
const char* WIFI_SSID = "";  // Your Wi-Fi SSID (empty: use the config stored in NVS by an earlier connection)
const char* WIFI_PASS = "";  // Your Wi-Fi password
// POSIX timezone string (default: JST-9 = UTC+9, no DST)
const char* YOUR_TIMEZONE = "JST-9";
// const char* YOUR_TIMEZONE = "EST5EDT,M3.2.0,M11.1.0";  // US Eastern (UTC-5, DST)
// const char* YOUR_TIMEZONE = "CST6CDT,M3.2.0,M11.1.0";  // US Central (UTC-6, DST)
// const char* YOUR_TIMEZONE = "PST8PDT,M3.2.0,M11.1.0";  // US Pacific (UTC-8, DST)
// const char* YOUR_TIMEZONE = "GMT0BST,M3.5.0/1,M10.5.0/2";    // UK (UTC+0, DST)
// const char* YOUR_TIMEZONE = "CET-1CEST,M3.5.0/2,M10.5.0/3";  // EU Central (UTC+1, DST)
// const char* YOUR_TIMEZONE = "CST-8";  // China (UTC+8, no DST)
// See: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

// NTP server list
// China
constexpr char ntp_cn0[] = "cn.pool.ntp.org";
constexpr char ntp_cn1[] = "ntp.aliyun.com";
constexpr char ntp_cn2[] = "time.cloud.tencent.com";
// Japan
constexpr char ntp_jp0[] = "ntp.nict.jp";
constexpr char ntp_jp1[] = "ntp.jst.mfeed.ad.jp";
constexpr char ntp_jp2[] = "time.google.com";
// United States
constexpr char ntp_us0[] = "us.pool.ntp.org";
constexpr char ntp_us1[] = "time.nist.gov";
constexpr char ntp_us2[] = "time.cloudflare.com";
// Europe
constexpr char ntp_eu0[] = "europe.pool.ntp.org";
constexpr char ntp_eu1[] = "ntp.ripe.net";
constexpr char ntp_eu2[] = "ptbtime1.ptb.de";

// Select region: jp / us / eu / cn
const char* ntpURLTable[] = {ntp_jp0, ntp_jp1, ntp_jp2};
// const char* ntpURLTable[] = {ntp_us0, ntp_us1, ntp_us2};
// const char* ntpURLTable[] = {ntp_eu0, ntp_eu1, ntp_eu2};
// const char* ntpURLTable[] = {ntp_cn0, ntp_cn1, ntp_cn2};

// UniformRandomBitGenerator using ESP32 hardware RNG
struct ESP32RNG {
    using result_type = uint32_t;
    static constexpr result_type min()
    {
        return 0;
    }
    static constexpr result_type max()
    {
        return UINT32_MAX;
    }
    result_type operator()()
    {
        return esp_random();
    }
};
constexpr char WDAY_NAMES[][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

auto& lcd = M5.Display;
m5::unit::UnitUnified Units;
#if defined(USING_UNIT_RTC)
#pragma message("Using Unit RTC")
m5::unit::UnitRTC unit;
#elif defined(BUILTIN_UNIT_RX8130)
#pragma message("Using the built-in RX8130 (Tab5)")
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "BUILTIN_UNIT_RX8130 is for Tab5 (ESP32-P4)"
#endif
m5::unit::UnitRX8130 unit;
#else
#error "Choose the RTC"
#endif

void display_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void display_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lcd.vprintf(fmt, args);
    va_end(args);
}

// Get the time from the NTP servers in ntpURLTable and set the system clock (needs Wi-Fi)
bool sync_sntp()
{
    constexpr uint32_t SYNC_TIMEOUT_MS{10000};
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    // esp_netif_sntp takes the TCP/IP core lock itself. Three servers need CONFIG_LWIP_SNTP_MAX_SERVERS >= 3
    // (sdkconfig.defaults for ESP-IDF; the Arduino core already uses 3)
    esp_sntp_config_t cfg =
        ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3, ESP_SNTP_SERVER_LIST(ntpURLTable[0], ntpURLTable[1], ntpURLTable[2]));
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        M5_LOGE("esp_netif_sntp_init failed");
        return false;
    }
    const bool synced = (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SYNC_TIMEOUT_MS)) == ESP_OK);
    esp_netif_sntp_deinit();
    return synced;
#else
    // ESP-IDF 4.4 (Arduino-ESP32 2.x)
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    for (int i = 0; i < 3; ++i) {
        sntp_setservername(i, ntpURLTable[i]);
    }
    sntp_init();
    const auto start_at = m5::utility::millis();
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED &&
           !m5::utility::hasElapsed(start_at, SYNC_TIMEOUT_MS)) {
        m5::utility::delay(100);
    }
    const bool synced = (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
    sntp_stop();
    return synced;
#endif
}

// Sync NTP time and set RTC (shows progress on Display)
bool sync_ntp_to_rtc()
{
    M5_LOGI("Connecting to Wi-Fi: %s", WIFI_SSID);
    display_printf("Wi-Fi connecting\n");
    if (!wifi_connect::connect(WIFI_SSID, WIFI_PASS)) {
        M5_LOGE("Wi-Fi connection failed");
        display_printf("Wi-Fi FAILED\n");
        wifi_connect::disconnect();
        return false;
    }
    M5_LOGI("Wi-Fi connected");
    display_printf("Wi-Fi OK\n");

    // Shuffle NTP servers
    std::shuffle(std::begin(ntpURLTable), std::end(ntpURLTable), ESP32RNG{});
    M5_LOGI("NTP: %s / %s / %s", ntpURLTable[0], ntpURLTable[1], ntpURLTable[2]);
    display_printf("NTP syncing\n");
    const bool synced = sync_sntp();

    // Done with Wi-Fi
    wifi_connect::disconnect();

    if (!synced) {
        M5_LOGE("Failed to get NTP time");
        display_printf("NTP FAILED\n");
        return false;
    }
    time_t now_local = time(nullptr);
    struct tm t      = *localtime(&now_local);
    M5_LOGI("NTP time: %04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
            t.tm_sec);
    display_printf("NTP OK\n");

    // Set RTC from NTP (use GMT for RTC storage)
    // Stop RTC clock for precise time setting (prescaler held in reset)
    unit.writeStop(true);

    // Prepare next second's time
    time_t now    = time(nullptr) + 1;
    struct tm gmt = *gmtime(&now);

    // Write time while clock is stopped (no carry race condition)
    if (!unit.writeDateTime(gmt)) {
        unit.writeStop(false);
        M5_LOGE("Failed to set RTC");
        display_printf("RTC set FAILED\n");
        return false;
    }

    // Wait for exact second boundary, then release STOP
    // First 1Hz tick occurs ~0.508s after STOP is released
    while (now > time(nullptr)) {
        ;
    }
    unit.writeStop(false);

    M5_LOGI("RTC set to (GMT): %04d-%02d-%02d %02d:%02d:%02d", gmt.tm_year + 1900, gmt.tm_mon + 1, gmt.tm_mday,
            gmt.tm_hour, gmt.tm_min, gmt.tm_sec);
    display_printf("RTC set OK\n");

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

#if defined(BUILTIN_UNIT_RX8130)
    // The built-in RX8130CE is on the internal I2C bus
    if (M5.getBoard() != m5::board_t::board_M5Tab5) {
        M5_LOGE("The board is not Tab5");
        m5::unit::wiring::failStop();
    }
    bool unit_ready = Units.add(unit, M5.In_I2C) && Units.begin();
#else
    // Board-aware connection: NessoN1 -> SoftwareI2C on the GROVE port, NanoC6/NanoH2 -> M5.Ex_I2C, others -> Wire
    bool unit_ready = m5::unit::wiring::addI2C(Units, unit) && Units.begin();
#endif
    if (!unit_ready) {
        M5_LOGE("Failed to begin");
        m5::unit::wiring::failStop();
    }
    M5_LOGI("M5UnitUnified initialized");
    M5_LOGI("%s", Units.debugInfo().c_str());

    // Check whether the time data were lost (backup voltage low / oscillation stop)
    bool volt_low{};
    if (unit.readVoltLow(volt_low) && volt_low) {
        M5_LOGW("RTC voltage low - time may be invalid");
    }

    // Local time zone for localtime() (the RTC keeps GMT)
    setenv("TZ", YOUR_TIMEZONE, 1);
    tzset();

    // Sync from NTP (Wi-Fi credentials in WIFI_SSID / WIFI_PASS, or the config stored in NVS)
    if (!sync_ntp_to_rtc()) {
        M5_LOGW("NTP sync skipped or failed - using current RTC time");
    }
    lcd.startWrite();
    lcd.fillScreen(0);
    lcd.endWrite();
}

void loop()
{
    static time_t prev{};

    M5.update();
    Units.update();

    // Show local time on Serial and Display every second
    time_t now = time(nullptr);
    if (now != prev) {
        struct tm local = *localtime(&now);

        // Serial
        M5.Log.printf("%04d-%02d-%02d(%s) %02d:%02d:%02d\n", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      WDAY_NAMES[local.tm_wday], local.tm_hour, local.tm_min, local.tm_sec);

        // Display
        lcd.setCursor(0, 0);
        lcd.startWrite();
        display_printf("%04d-%02d-%02d(%s)\n%02d:%02d:%02d\n", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                       WDAY_NAMES[local.tm_wday], local.tm_hour, local.tm_min, local.tm_sec);
        lcd.endWrite();
        prev = now;
    }

    if (M5.BtnA.wasClicked()) {
        // Read and show RTC (GMT)
        m5::unit::pcf8563::rtc_datetime_t dt{};
        lcd.startWrite();
        if (unit.readDateTime(dt)) {
            const char* wday{(dt.date.weekDay >= 0 && dt.date.weekDay <= 6) ? WDAY_NAMES[dt.date.weekDay] : "---"};
            M5.Log.printf("RTC(GMT) %04d-%02d-%02d(%s) %02d:%02d:%02d\n", dt.date.year, dt.date.month, dt.date.date,
                          wday, dt.time.hours, dt.time.minutes, dt.time.seconds);
            display_printf("RTC(GMT)\n%04d-%02d-%02d(%s)\n%02d:%02d:%02d\n", dt.date.year, dt.date.month, dt.date.date,
                           wday, dt.time.hours, dt.time.minutes, dt.time.seconds);
        } else {
            M5_LOGE("Failed to read the RTC");
            display_printf("RTC read failed\n");
        }
        lcd.endWrite();
    } else if (M5.BtnA.wasHold()) {
        // Re-sync RTC from NTP (progress shown on Display)
        M5_LOGI("Re-syncing NTP...");
        lcd.startWrite();
        lcd.fillScreen(0);
        lcd.endWrite();

        lcd.setCursor(0, 0);
        if (!sync_ntp_to_rtc()) {
            M5_LOGW("NTP sync failed");
        }
        lcd.startWrite();
        lcd.fillScreen(0);
        lcd.endWrite();
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
