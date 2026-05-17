add_executable(u273_bench main.cpp)
target_link_libraries(u273_bench PRIVATE u273::reference u273::core u273::dsp)
u273_target_defaults(u273_bench)
set_target_properties(u273_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/u273_bench")

target_compile_definitions(u273_bench PRIVATE
    U273_DEFAULT_DATASET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../results")
