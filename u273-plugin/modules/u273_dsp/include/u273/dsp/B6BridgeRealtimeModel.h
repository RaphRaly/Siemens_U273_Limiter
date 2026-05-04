#pragma once

#include "u273/core/ParameterSnapshot.h"
#include "u273/dsp/GainReductionComputer.h"
#include "u273/dsp/RealtimeGainReductionModel.h"

namespace u273::dsp {

class [[deprecated("Use AnalogRealtimeEngine for the realtime U273 analog path.")]] B6BridgeRealtimeModel final
    : public RealtimeGainReductionModel {
public:
    void prepare(double sampleRate) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] float evaluateGainReductionDb(float detectorEnvelope,
                                                const u273::core::ParameterSnapshot& snapshot) noexcept override;

    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept override
    {
        return u273::core::ModelBoundary::guardedRealtimeSurrogate;
    }

private:
    double sampleRate_ {48000.0};
    GainReductionComputer gainReduction_ {};
};

} // namespace u273::dsp
