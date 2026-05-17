#pragma once

#include "u273/core/ModelBoundary.h"

namespace u273::dsp {

enum class RealtimeDetailLevel {
    sd = 0,
    md,
    hd,
    uhd
};

struct RealtimeDetailLevelInfo {
    RealtimeDetailLevel level {};
    const char* label {};
    u273::core::ModelBoundary boundary {};
    int controlOversamplingFactor {};
    bool realtime {};
};

[[nodiscard]] constexpr RealtimeDetailLevelInfo detailLevelInfo(RealtimeDetailLevel level) noexcept
{
    switch (level) {
    case RealtimeDetailLevel::sd:
        return {level, "SD", u273::core::ModelBoundary::fullActiveModelUnverified, 1, true};
    case RealtimeDetailLevel::md:
        return {level, "MD", u273::core::ModelBoundary::guardedRealtimeSurrogate, 1, true};
    case RealtimeDetailLevel::hd:
        return {level, "HD", u273::core::ModelBoundary::guardedRealtimeSurrogate, 2, true};
    case RealtimeDetailLevel::uhd:
        return {level, "UHD", u273::core::ModelBoundary::fullActiveModelUnverified, 8, false};
    }
    return {RealtimeDetailLevel::sd, "SD", u273::core::ModelBoundary::fullActiveModelUnverified, 1, true};
}

} // namespace u273::dsp
