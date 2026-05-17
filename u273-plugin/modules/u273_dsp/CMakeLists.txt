add_library(u273_dsp STATIC
    include/u273/dsp/FullActiveRealtimeEngine.h
    include/u273/dsp/RateGraph.h
    include/u273/dsp/RealtimeGainReductionModel.h
    include/u273/dsp/RealtimeDetailLevel.h
    include/u273/dsp/TableReductionRealtimeEngine.h
    src/AnalogRealtimeEngine.cpp
    src/B6BridgeRealtimeModel.cpp
    src/DetectorEnvelope.cpp
    src/FullActiveRealtimeEngine.cpp
    src/GainReductionComputer.cpp
    src/RateGraph.cpp
    src/TableReductionRealtimeEngine.cpp
    src/U273DspEngine.cpp)

add_library(u273::dsp ALIAS u273_dsp)

target_include_directories(u273_dsp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_link_libraries(u273_dsp PUBLIC u273::core)

u273_target_defaults(u273_dsp)
