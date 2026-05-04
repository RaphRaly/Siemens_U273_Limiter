#include "u273/dsp/B6BridgeRealtimeModel.h"

#include <algorithm>

namespace u273::dsp {

void B6BridgeRealtimeModel::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
}

void B6BridgeRealtimeModel::reset() noexcept
{
}

float B6BridgeRealtimeModel::evaluateGainReductionDb(float detectorEnvelope,
                                                     const u273::core::ParameterSnapshot& snapshot) noexcept
{
    return gainReduction_.computeGainReductionDb(detectorEnvelope, snapshot);
}

} // namespace u273::dsp
