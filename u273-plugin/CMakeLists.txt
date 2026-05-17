cmake_minimum_required(VERSION 3.22)

project(U273Plugin
    VERSION 0.1.0
    DESCRIPTION "Siemens U273 guarded scientific realtime plugin scaffold"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(U273Compiler)

option(U273_BUILD_PLUGIN "Build the JUCE VST3/AU plugin wrapper" ON)
option(U273_BUILD_TESTS "Build the C++ unit/regression scaffold tests" ON)

set(U273_JUCE_SOURCE_DIR "C:/Users/user/JuceProgram/JUCE" CACHE PATH
    "Path to a local JUCE source checkout containing JUCE CMakeLists.txt")

if(U273_BUILD_PLUGIN)
    if(NOT EXISTS "${U273_JUCE_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "U273_BUILD_PLUGIN is ON, but U273_JUCE_SOURCE_DIR does not point "
            "to a JUCE source tree. Set -DU273_JUCE_SOURCE_DIR=...")
    endif()

    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${U273_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/juce" EXCLUDE_FROM_ALL)
endif()

add_subdirectory(modules/u273_core)
add_subdirectory(modules/u273_assets)
add_subdirectory(modules/u273_dsp)
add_subdirectory(modules/u273_reference)

if(U273_BUILD_PLUGIN)
    add_subdirectory(modules/u273_plugin)
endif()

if(U273_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(U273_BUILD_BENCH_TOOL "Build the u273_bench CLI (scientific demo bench)" ON)
if(U273_BUILD_BENCH_TOOL)
    add_subdirectory(tools/u273_bench)
endif()

option(U273_BUILD_AC_PROBE "Build the u273_ac_probe CLI (per-frequency AC residual diagnostic)" ON)
if(U273_BUILD_AC_PROBE)
    add_subdirectory(tools/u273_ac_probe)
endif()
