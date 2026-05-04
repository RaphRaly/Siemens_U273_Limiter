#pragma once

#include <cstdint>

#include "u273/core/Constants.h"
#include "u273/core/Math.h"
#include "u273/core/ModelBoundary.h"

namespace u273::core {

enum class U273Mode : std::uint8_t {
    guardedRealtime = 0,
    bypass = 1
};

// Range contract shared by host parameters, snapshots, and DSP validation.
struct ParameterRange {
    float minimum {};
    float maximum {};
    float defaultValue {};

    [[nodiscard]] bool contains(float value) const noexcept
    {
        return isFinite(value) && value >= minimum && value <= maximum;
    }
};

struct ParameterRanges {
    static constexpr ParameterRange inputGainDb {-24.0f, 24.0f, 0.0f};
    static constexpr ParameterRange outputGainDb {-24.0f, 24.0f, 0.0f};
    static constexpr ParameterRange drive {0.0f, 1.0f, 0.35f};
    static constexpr ParameterRange detectorScale {0.125f, 4.0f, 1.0f};
    static constexpr ParameterRange attackMs {0.05f, 500.0f, 3.0f};
    static constexpr ParameterRange releaseMs {1.0f, 3000.0f, 160.0f};
    static constexpr ParameterRange mix {0.0f, 1.0f, 1.0f};
    static constexpr ParameterRange calibrationLevelDb {-36.0f, 36.0f, 0.0f};
};

// Immutable parameter packet consumed by the realtime engine. The plugin layer
// translates host atomics into this plain value type before audio processing.
struct ParameterSnapshot {
    float inputGainDb {ParameterRanges::inputGainDb.defaultValue};
    float outputGainDb {ParameterRanges::outputGainDb.defaultValue};
    float drive {ParameterRanges::drive.defaultValue};
    float detectorScale {ParameterRanges::detectorScale.defaultValue};
    float attackMs {ParameterRanges::attackMs.defaultValue};
    float releaseMs {ParameterRanges::releaseMs.defaultValue};
    float mix {ParameterRanges::mix.defaultValue};
    float calibrationLevelDb {ParameterRanges::calibrationLevelDb.defaultValue};
    U273Mode mode {U273Mode::guardedRealtime};
    bool bypass {false};
    std::uint32_t version {kParameterSnapshotVersion};
    ModelBoundary modelBoundary {currentScientificBoundary()};

    [[nodiscard]] bool isValid() const noexcept
    {
        // Reject unknown versions or out-of-range values before they enter the
        // realtime loop; the DSP then avoids per-parameter defensive branches.
        return version == kParameterSnapshotVersion
            && ParameterRanges::inputGainDb.contains(inputGainDb)
            && ParameterRanges::outputGainDb.contains(outputGainDb)
            && ParameterRanges::drive.contains(drive)
            && ParameterRanges::detectorScale.contains(detectorScale)
            && ParameterRanges::attackMs.contains(attackMs)
            && ParameterRanges::releaseMs.contains(releaseMs)
            && ParameterRanges::mix.contains(mix)
            && ParameterRanges::calibrationLevelDb.contains(calibrationLevelDb)
            && modelBoundary != ModelBoundary::unknown;
    }

    [[nodiscard]] bool isBypassed() const noexcept
    {
        return bypass || mode == U273Mode::bypass;
    }
};

} // namespace u273::core
