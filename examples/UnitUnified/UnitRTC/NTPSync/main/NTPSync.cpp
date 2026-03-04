/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  Example using M5UnitUnified for UnitRTC (PCF8563/BM8563/HYM8563)
  - Sync time from NTP via WiFi, then set RTC
*/
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedRTC.h>
#include <M5HAL.hpp>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_random.h>
#include <algorithm>  // std::shuffle
#include <cstdint>

namespace {
// ==============================================================
// NOTICE: Set your WiFi credentials and timezone before building
// ==============================================================
const char* WIFI_SSID = "";  // Your WiFi SSID
const char* WIFI_PASS = "";  // Your WiFi password
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
m5::unit::UnitRTC unit;

void display_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void display_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lcd.vprintf(fmt, args);
    va_end(args);
}

// Sync NTP time and set RTC (shows progress on Display)
bool sync_ntp_to_rtc()
{
    M5_LOGI("Connecting to WiFi: %s", WIFI_SSID);
    display_printf("WiFi connecting");

    // Tab5 (ESP32-P4) uses ESP32-C6 co-processor for WiFi via SDIO; set pins before WiFi.begin()
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    if (M5.getBoard() == m5::board_t::board_M5Tab5) {
        constexpr int SDIO2_CLK = 12;
        constexpr int SDIO2_CMD = 13;
        constexpr int SDIO2_D0  = 11;
        constexpr int SDIO2_D1  = 10;
        constexpr int SDIO2_D2  = 9;
        constexpr int SDIO2_D3  = 8;
        constexpr int SDIO2_RST = 15;
        WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
    }
#endif

    WiFi.mode(WIFI_STA);
    if (WIFI_SSID[0] && WIFI_PASS[0]) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    } else {
        WiFi.begin();
    }

    int retry = 20;
    while (retry-- > 0 && WiFi.status() != WL_CONNECTED) {
        display_printf(".");
        m5::utility::delay(500);
    }
    if (WiFi.status() != WL_CONNECTED) {
        M5_LOGE("WiFi connection failed");
        display_printf("\nWiFi FAILED\n");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
    M5_LOGI("WiFi connected");
    display_printf("\nWiFi OK\n");

    // Shuffle NTP servers
    std::shuffle(std::begin(ntpURLTable), std::end(ntpURLTable), ESP32RNG{});
    M5_LOGI("NTP: %s / %s / %s", ntpURLTable[0], ntpURLTable[1], ntpURLTable[2]);
    configTzTime(YOUR_TIMEZONE, ntpURLTable[0], ntpURLTable[1], ntpURLTable[2]);

    // Wait for sync
    display_printf("NTP syncing");
    retry = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && --retry >= 0) {
        M5_LOGI("  NTP sync in progress...");
        display_printf(".");
        m5::utility::delay(1000);
    }

    // Done with WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    struct tm t {
    };
    if (!getLocalTime(&t, 10000)) {
        M5_LOGE("Failed to get NTP time");
        display_printf("\nNTP FAILED\n");
        return false;
    }
    M5_LOGI("NTP time: %04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
            t.tm_sec);
    display_printf("\nNTP OK\n");

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

    auto board = M5.getBoard();

    // NessoN1: Arduino Wire (I2C_NUM_0) cannot be used for GROVE port.
    //   Wire is used by M5Unified In_I2C for internal devices (IOExpander etc.).
    //   Wire1 exists but is reserved for HatPort — cannot be used for GROVE.
    //   Reconfiguring Wire to GROVE pins breaks In_I2C, causing ESP_ERR_INVALID_STATE in M5.update().
    //   Solution: Use SoftwareI2C via M5HAL (bit-banging) for the GROVE port.
    // NanoC6: Wire.begin() on GROVE pins conflicts with m5gfx::i2c registered by Ex_I2C.setPort()
    //   on the same I2C_NUM_0, causing sporadic NACK errors.
    //   Solution: Use M5.Ex_I2C (m5gfx::i2c) directly instead of Arduino Wire.
    bool unit_ready{};
    if (board == m5::board_t::board_ArduinoNessoN1) {
        // NessoN1: GROVE is on port_b (GPIO 5/4), not port_a (which maps to Wire pins 8/10)
        auto pin_num_sda = M5.getPin(m5::pin_name_t::port_b_out);
        auto pin_num_scl = M5.getPin(m5::pin_name_t::port_b_in);
        M5_LOGI("getPin(M5HAL): SDA:%u SCL:%u", pin_num_sda, pin_num_scl);
        m5::hal::bus::I2CBusConfig i2c_cfg;
        i2c_cfg.pin_sda = m5::hal::gpio::getPin(pin_num_sda);
        i2c_cfg.pin_scl = m5::hal::gpio::getPin(pin_num_scl);
        auto i2c_bus    = m5::hal::bus::i2c::getBus(i2c_cfg);
        M5_LOGI("Bus:%d", i2c_bus.has_value());
        unit_ready = Units.add(unit, i2c_bus ? i2c_bus.value() : nullptr) && Units.begin();
    } else if (board == m5::board_t::board_M5NanoC6) {
        M5_LOGI("Using M5.Ex_I2C");
        unit_ready = Units.add(unit, M5.Ex_I2C) && Units.begin();
    } else {
        auto pin_num_sda = M5.getPin(m5::pin_name_t::port_a_sda);
        auto pin_num_scl = M5.getPin(m5::pin_name_t::port_a_scl);
        M5_LOGI("getPin: SDA:%u SCL:%u", pin_num_sda, pin_num_scl);
        Wire.end();
        Wire.begin(pin_num_sda, pin_num_scl, 100 * 1000U);
        unit_ready = Units.add(unit, Wire) && Units.begin();
    }
    if (!unit_ready) {
        M5_LOGE("Failed to begin");
        while (true) {
            m5::utility::delay(10000);
        }
    }

    M5_LOGI("M5UnitUnified has been begun");
    M5_LOGI("%s", Units.debugInfo().c_str());

    // Check voltage low (battery backup lost)
    if (unit.getVoltLow()) {
        M5_LOGW("RTC voltage low - time may be invalid");
    }

    // Sync from NTP if WiFi credentials are configured
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
        auto dt = unit.getDateTime();
        M5.Log.printf("RTC(GMT) %04d-%02d-%02d(%s) %02d:%02d:%02d\n", dt.date.year, dt.date.month, dt.date.date,
                      WDAY_NAMES[dt.date.weekDay % 7], dt.time.hours, dt.time.minutes, dt.time.seconds);
        lcd.startWrite();
        display_printf("RTC(GMT)\n%04d-%02d-%02d(%s)\n%02d:%02d:%02d\n", dt.date.year, dt.date.month, dt.date.date,
                       WDAY_NAMES[dt.date.weekDay % 7], dt.time.hours, dt.time.minutes, dt.time.seconds);
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
