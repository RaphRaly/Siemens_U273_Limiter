#pragma once

#include <cstdint>

#include "u273/core/AudioBlockView.h"
#include "u273/core/Math.h"

namespace u273::core {

// Per-block processing metadata. Audio memory is referenced, not owned, so the
// DSP can run without allocations or host-framework dependencies.
struct ProcessContext {
    AudioBlockView audio {};
    double sampleRate {kDefaultSampleRate};
    std::uint64_t blockSequence {};
    bool realtimeThread {true};

    [[nodiscard]] bool isValid() const noexcept
    {
        // The sample rate is part of the runtime contract even when the current
        // block does not need rate-dependent processing.
        return audio.isValid() && isFinite(sampleRate) && sampleRate > 0.0;
    }
};

} // namespace u273::core
