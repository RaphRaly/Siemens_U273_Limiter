#pragma once

#include "u273/core/ModelBoundary.h"
#include "u273/core/ParameterSnapshot.h"

namespace u273::dsp {

// Small realtime-safe contract used by U273DspEngine to avoid hard-wiring a
// specific analog model into the audio processing loop.
class RealtimeGainReductionModel {
public:
    virtual ~RealtimeGainReductionModel() = default;

    virtual void prepare(double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;
    [[nodiscard]] virtual float evaluateGainReductionDb(
        float detectorEnvelope,
        const u273::core::ParameterSnapshot& snapshot) noexcept = 0;
    [[nodiscard]] virtual u273::core::ModelBoundary boundary() const noexcept = 0;
};

} // namespace u273::dsp
