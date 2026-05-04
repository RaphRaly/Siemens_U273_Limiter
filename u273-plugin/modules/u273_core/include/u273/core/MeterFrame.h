#pragma once

#include <cstdint>

#include "u273/core/Constants.h"
#include "u273/core/Math.h"

namespace u273::core {

// Lock-free meter payload published by the audio thread and read by the UI.
// Sequence allows the reader to detect fresh frames without touching DSP state.
struct MeterFrame {
    float inputPeakDb {kMinMeterDb};
    float outputPeakDb {kMinMeterDb};
    float gainReductionDb {0.0f};
    bool clipFlag {false};
    std::uint64_t sequence {};

    [[nodiscard]] bool isValid() const noexcept
    {
        return isFinite(inputPeakDb)
            && isFinite(outputPeakDb)
            && isFinite(gainReductionDb)
            && gainReductionDb >= 0.0f;
    }
};

} // namespace u273::core
