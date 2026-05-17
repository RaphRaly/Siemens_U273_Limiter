add_executable(u273_ac_probe main.cpp)
target_link_libraries(u273_ac_probe PRIVATE u273::reference u273::core)
u273_target_defaults(u273_ac_probe)
set_target_properties(u273_ac_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/u273_ac_probe")

target_compile_definitions(u273_ac_probe PRIVATE
    U273_DEFAULT_DATASET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../results")
