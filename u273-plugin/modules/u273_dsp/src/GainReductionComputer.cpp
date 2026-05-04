#include "u273/dsp/GainReductionComputer.h"

#include <algorithm>

#include "u273/core/Math.h"

namespace u273::dsp {

float GainReductionComputer::computeGainReductionDb(float detectorEnvelope,
                                                    const u273::core::ParameterSnapshot& snapshot) const noexcept
{
    if (snapshot.isBypassed()) {
        return 0.0f;
    }

    const auto envelopeDb = u273::core::linearToDb(detectorEnvelope);

    const auto driveDb = snapshot.drive * 36.0f;
    constexpr auto thresholdDb = -24.0f;
    constexpr auto kneeDb = 6.0f;

    const auto overDb = envelopeDb + driveDb - snapshot.calibrationLevelDb - thresholdDb;
    const auto kneeOverDb = softKneeOverDb(overDb, kneeDb);
    const auto ratio = 1.5f + snapshot.drive * 18.5f;
    const auto reductionDb = kneeOverDb * (1.0f - (1.0f / ratio));

    return u273::core::clampFloat(reductionDb, 0.0f, 36.0f);
}

float GainReductionComputer::softKneeOverDb(float overDb, float kneeDb) noexcept
{
    if (overDb <= -kneeDb * 0.5f) {
        return 0.0f;
    }

    if (overDb >= kneeDb * 0.5f) {
        return overDb;
    }

    const auto shifted = overDb + kneeDb * 0.5f;
    return (shifted * shifted) / (2.0f * kneeDb);
}

} // namespace u273::dsp
