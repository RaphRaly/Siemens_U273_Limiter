add_library(u273_assets INTERFACE)
add_library(u273::assets ALIAS u273_assets)

target_compile_definitions(u273_assets INTERFACE
    U273_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
