# Dependencies are pulled in via main/idf_component.yml (and the library's own idf_component.yml)
# and auto-added as requirements by the component manager.
idf_component_register(
    SRCS "AlarmNotify.cpp"
    INCLUDE_DIRS ".")
