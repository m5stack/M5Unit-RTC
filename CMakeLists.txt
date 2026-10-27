# ESP-IDF component registration for M5Unit-RTC.
# Upstream: https://github.com/m5stack/M5Unit-RTC
#
# The public headers include M5UnitUnified (M5UnitComponent.hpp) only; M5Utility and the GPIO driver are
# used by the .cpp files. The legacy Arduino library (src/Unit_RTC.*, uses Wire) is not built.
file(GLOB_RECURSE SRCS "src/*.cpp")
list(FILTER SRCS EXCLUDE REGEX "/src/Unit_RTC\\.cpp$")
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "src"
    REQUIRES M5UnitUnified
    PRIV_REQUIRES M5Utility driver
)

# The M5Unified compatible API (M5.Rtc style getDateTime / setAlarmIRQ ...) is compiled when M5Unified is part of
# the build (e.g. a M5Unified-based app or example). Pure ESP-IDF builds without M5Unified skip it.
# The component name depends on where M5Unified comes from, so list every form (see M5UnitUnified's CMakeLists.txt):
# m5stack__m5unified (ESP Component Registry), m5stack__M5Unified (git dependency) and M5Unified (local components/).
idf_component_optional_requires(PRIVATE m5stack__m5unified m5stack__M5Unified M5Unified)
