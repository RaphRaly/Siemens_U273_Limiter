add_library(u273_core INTERFACE)
add_library(u273::core ALIAS u273_core)

target_include_directories(u273_core INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

u273_target_defaults(u273_core)
