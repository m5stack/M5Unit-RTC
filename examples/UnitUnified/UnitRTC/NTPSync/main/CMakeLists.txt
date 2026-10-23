# Dependencies are pulled in via main/idf_component.yml (and the library's own idf_component.yml)
# and auto-added as requirements by the component manager.
idf_component_register(
    SRCS "NTPSync.cpp"
    INCLUDE_DIRS ".")

# The RTC variant is selected via Kconfig (main/Kconfig.projbuild -> common/Kconfig.variant.ntpsync).
# Include variant.cmake after the register call (it needs ${COMPONENT_LIB}).
include("${CMAKE_CURRENT_LIST_DIR}/../../../common/variant.cmake")
