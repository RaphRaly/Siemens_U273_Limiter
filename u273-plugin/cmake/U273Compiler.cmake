function(u273_target_defaults target_name)
    get_target_property(target_type "${target_name}" TYPE)

    if(target_type STREQUAL "INTERFACE_LIBRARY")
        target_compile_features("${target_name}" INTERFACE cxx_std_20)
        return()
    endif()

    target_compile_features("${target_name}" PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options("${target_name}" PRIVATE
            /W4
            /permissive-
            /EHsc
            /Zc:__cplusplus)
        target_compile_definitions("${target_name}" PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options("${target_name}" PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion)
    endif()
endfunction()
