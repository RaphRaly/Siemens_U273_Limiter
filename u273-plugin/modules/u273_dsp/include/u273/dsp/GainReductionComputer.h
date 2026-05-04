#pragma once

#include "u273/core/ParameterSnapshot.h"

namespace u273::dsp {

// Legacy guarded compressor law kept for comparison with the newer analog
// realtime bridge. It stays isolated behind the realtime model abstraction.
class GainReductionComputer {
public:
    [[nodiscard]] float computeGainReductionDb(float detectorEnvelope,
                                               const u273::core::ParameterSnapshot& snapshot) const noexcept;

private:
    [[nodiscard]] static float softKneeOverDb(float overDb, float kneeDb) noexcept;
};

} // namespace u273::dsp
