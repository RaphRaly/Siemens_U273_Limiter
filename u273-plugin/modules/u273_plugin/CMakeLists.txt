set(U273_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND U273_PLUGIN_FORMATS AU)
endif()
list(APPEND U273_PLUGIN_FORMATS Standalone)

juce_add_plugin(u273_plugin
    COMPANY_NAME "U273 Research"
    COMPANY_WEBSITE "https://example.invalid/u273"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Si73
    PLUGIN_CODE U273
    FORMATS ${U273_PLUGIN_FORMATS}
    PRODUCT_NAME "U273 Limiter")

target_sources(u273_plugin PRIVATE
    include/u273/plugin/MeterBridge.h
    include/u273/plugin/ParameterBridge.h
    include/u273/plugin/PluginEditor.h
    include/u273/plugin/PluginProcessor.h
    include/u273/plugin/PresetManager.h
    src/MeterBridge.cpp
    src/ParameterBridge.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.cpp
    src/PresetManager.cpp)

target_include_directories(u273_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(u273_plugin PRIVATE
    u273::core
    u273::dsp
    u273::assets
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(u273_plugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

u273_target_defaults(u273_plugin)

if(TARGET u273_plugin_Standalone)
    set(U273_CURRENT_STANDALONE_DIR "${CMAKE_SOURCE_DIR}/../standAloneActuel")
    add_custom_command(TARGET u273_plugin_Standalone POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${U273_CURRENT_STANDALONE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:u273_plugin_Standalone>"
            "${U273_CURRENT_STANDALONE_DIR}/U273 Limiter.exe"
        COMMENT "Refreshing current Standalone demo at ${U273_CURRENT_STANDALONE_DIR}")
endif()
