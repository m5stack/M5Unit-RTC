# Map the Kconfig "Target RTC" choice (common/Kconfig.variant.*) to the source-level macro that
# both the Arduino and ESP-IDF builds use. Include this from an example's main/CMakeLists.txt
# *after* idf_component_register() (it needs ${COMPONENT_LIB}). Only the examples that have a
# variant (UnitRTC/NTPSync) include this file.
set(M5UNIT_VARIANT "")
# UnitRTC/NTPSync (common/Kconfig.variant.ntpsync)
if(CONFIG_EXAMPLE_USING_UNIT_RTC)
    set(M5UNIT_VARIANT USING_UNIT_RTC)
elseif(CONFIG_EXAMPLE_BUILTIN_UNIT_RX8130)
    set(M5UNIT_VARIANT BUILTIN_UNIT_RX8130)
endif()
if(M5UNIT_VARIANT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${M5UNIT_VARIANT})
endif()
