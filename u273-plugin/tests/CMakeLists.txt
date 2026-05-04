add_executable(u273_tests
    main.cpp)

target_link_libraries(u273_tests PRIVATE
    u273::core
    u273::dsp
    u273::reference
    u273::assets)

target_compile_definitions(u273_tests PRIVATE
    U273_RESULTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../results")

u273_target_defaults(u273_tests)

add_test(NAME u273_tests COMMAND u273_tests)
