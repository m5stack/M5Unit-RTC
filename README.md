# M5Unit-RTC

## Overview

Library for Unit RTC and the built-in RX8130CE RTC using [M5UnitUnified](https://github.com/m5stack/M5UnitUnified).  
M5UnitUnified is a library for unified handling of various M5 units products.

### SKU:U126 (Unit RTC)

Unit RTC is a programmable real-time clock module that integrates the HYM8563 low-power CMOS real-time clock/calendar chip.
It uses the I2C communication interface (address 0x51). This library also supports the compatible BM8563 / PCF8563 (UnitPCF8563).

### Built-in RX8130CE

The Epson RX8130CE real-time clock built into M5Stack boards such as Tab5 (UnitRX8130, on the internal I2C bus, address 0x32).

## Related Link

- [Unit RTC & Datasheet](https://docs.m5stack.com/en/products/sku/U126)

## Required Libraries

- [M5UnitUnified](https://github.com/m5stack/M5UnitUnified)
- [M5Utility](https://github.com/m5stack/M5Utility)
- [M5HAL](https://github.com/m5stack/M5HAL)

## License

- [M5Unit-RTC - MIT](LICENSE)

## Include file

```cpp
#include <M5UnitUnifiedRTC.h> // For UnitUnified
```

## Examples
See also [examples/UnitUnified](examples/UnitUnified)

- [UnitRTC/NTPSync](examples/UnitUnified/UnitRTC/NTPSync): Gets the time from NTP over Wi-Fi and sets the RTC
- [UnitRTC/AlarmNotify](examples/UnitUnified/UnitRTC/AlarmNotify): Sets an alarm one minute ahead and notifies when it fires
- [UnitRTC/PeriodicTimer](examples/UnitUnified/UnitRTC/PeriodicTimer): Fires the countdown timer periodically
- [UnitRTC/ViaPaHub](examples/UnitUnified/UnitRTC/ViaPaHub): Reads the date and time through PaHub (ch:0).
  Requires [M5Unit-HUB](https://github.com/m5stack/M5Unit-HUB).

### For ESP-IDF settings

> **NOTE:** The ESP-IDF native build (`idf.py`) targets ESP-IDF **5.1 or later** (5.x and 6.x).
> The legacy library (`Unit_RTC.h`) is for Arduino only and is not built under ESP-IDF.

The examples need no Kconfig choice. Build them directly:

```sh
cd examples/UnitUnified/UnitRTC/PeriodicTimer
idf.py set-target esp32s3               # or esp32 / esp32c5 / esp32c6 / esp32h2 / esp32p4 / ...
idf.py build flash monitor
```

NTPSync uses Wi-Fi, so it does not support ESP32-H2 (NanoH2). On ESP32-P4 (Tab5) the Wi-Fi runs on the ESP32-C6
co-processor (esp_hosted), which needs ESP-IDF 5.3 or later.

## Support via [PaHub](https://docs.m5stack.com/en/unit/Unit-PaHub%20v2.1)

|Unit|Support|Note|
|---|---|---|
|UnitRTC|OK||

See also the [ViaPaHub](examples/UnitUnified/UnitRTC/ViaPaHub) example and [M5Unit-HUB](https://github.com/m5stack/M5Unit-HUB)

## Doxygen document
[GitHub Pages](https://m5stack.github.io/M5Unit-RTC/)

If you want to generate documents on your local machine, execute the following command

```
bash docs/doxy.sh
```

It will output it under docs/html  
If you want to output Git commit hashes to html, do it for the git cloned folder.

### Required
- [Doxygen](https://www.doxygen.nl/)
- [Git](https://git-scm.com/) (Output commit hash to html)

## Legacy library

The legacy library (`Unit_RTC`) is a standalone Arduino driver for Unit RTC that does not depend on M5UnitUnified.
When using M5UnitUnified, do not use it at the same time as the legacy library.

### Include file
```cpp
#include <Unit_RTC.h>
```

### Examples
- [Unit_RTC_M5Series](examples/Unit_RTC_M5Series)
- [Unit_RTC_M5Core](examples/Unit_RTC_M5Core)
- [Unit_RTC_M5Core2](examples/Unit_RTC_M5Core2)
- [Unit_RTC_M5Atom](examples/Unit_RTC_M5Atom)
- [Unit_RTC_M5StickC](examples/Unit_RTC_M5StickC)
- [Unit_RTC_M5StickCPlus](examples/Unit_RTC_M5StickCPlus)
