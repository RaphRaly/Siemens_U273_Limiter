#include "u273/dsp/DetectorEnvelope.h"

#include <algorithm>
#include <cmath>

namespace u273::dsp {

void DetectorEnvelope::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    setTimeConstants(3.0f, 160.0f);
    reset();
}

void DetectorEnvelope::reset(float value) noexcept
{
    envelope_ = std::max(0.0f, value);
}

void DetectorEnvelope::setTimeConstants(float attackMs, float releaseMs) noexcept
{
    attackCoefficient_ = coefficient(sampleRate_, attackMs);
    releaseCoefficient_ = coefficient(sampleRate_, releaseMs);
}

float DetectorEnvelope::processSample(float inputAbs) noexcept
{
    const auto target = std::max(0.0f, inputAbs);
    const auto coeff = target > envelope_ ? attackCoefficient_ : releaseCoefficient_;
    envelope_ = target + coeff * (envelope_ - target);
    return envelope_;
}

float DetectorEnvelope::coefficient(double sampleRate, float timeMs) noexcept
{
    const auto boundedMs = std::max(0.001f, timeMs);
    const auto tauSeconds = static_cast<double>(boundedMs) * 0.001;
    return static_cast<float>(std::exp(-1.0 / (tauSeconds * std::max(1.0, sampleRate))));
}

} // namespace u273::dsp
