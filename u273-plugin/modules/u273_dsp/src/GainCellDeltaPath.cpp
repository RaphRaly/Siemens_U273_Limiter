#include "u273/dsp/GainCellDeltaPath.h"

#include <algorithm>

#include "u273/core/Math.h"

namespace u273::dsp {

bool GainCellDeltaPath::prepare(int oversamplingFactor, int numChannels) noexcept
{
    prepared_ = false;
    oversamplingFactor_ = 1;
    numChannels_ = 0;

    if (!LinearPhaseFirResampler::isSupportedFactor(oversamplingFactor)
        || numChannels <= 0
        || numChannels > u273::core::kMaxRealtimeChannels) {
        reset();
        return false;
    }

    oversamplingFactor_ = std::max(1, oversamplingFactor);
    numChannels_ = numChannels;

    for (auto channel = 0; channel < u273::core::kMaxRealtimeChannels; ++channel) {
        if (!channels_[static_cast<std::size_t>(channel)].reference.prepare(oversamplingFactor_)
            || !channels_[static_cast<std::size_t>(channel)].processed.prepare(oversamplingFactor_)) {
            oversamplingFactor_ = 1;
            numChannels_ = 0;
            reset();
            return false;
        }
        channels_[static_cast<std::size_t>(channel)].dryDelay.prepare(latencySamples());
    }

    reset();
    prepared_ = true;
    return true;
}

void GainCellDeltaPath::reset() noexcept
{
    for (auto& channel : channels_) {
        channel.reference.reset();
        channel.processed.reset();
        channel.dryDelay.reset();
    }
}

float GainCellDeltaPath::processSample(int channel, float dry, float wetGain, float mix) noexcept
{
    if (!prepared_
        || channel < 0
        || channel >= numChannels_
        || !u273::core::isFinite(dry)) {
        return dry;
    }

    const auto boundedMix = u273::core::clampFloat(mix, 0.0f, 1.0f);
    const auto safeGain = u273::core::isFinite(wetGain) ? wetGain : 1.0f;

    if (oversamplingFactor_ <= 1) {
        const auto wet = dry * safeGain;
        return dry + (wet - dry) * boundedMix;
    }

    auto& state = channels_[static_cast<std::size_t>(channel)];
    const auto dryDelayed = state.dryDelay.process(dry);
    const auto referenceDown = state.reference.process(dry, 1.0f);
    const auto processedDown = state.processed.process(dry, safeGain);
    const auto delta = processedDown - referenceDown;
    return dryDelayed + delta * boundedMix;
}

void GainCellDeltaPath::DryDelay::prepare(int delaySamples) noexcept
{
    delaySamples_ = std::clamp(delaySamples, 0, kMaxDelaySamples);
    reset();
}

void GainCellDeltaPath::DryDelay::reset() noexcept
{
    buffer_.fill(0.0f);
    writeIndex_ = 0;
}

float GainCellDeltaPath::DryDelay::process(float input) noexcept
{
    if (delaySamples_ <= 0) {
        return input;
    }

    auto readIndex = writeIndex_ - delaySamples_;
    while (readIndex < 0) {
        readIndex += static_cast<int>(buffer_.size());
    }

    const auto output = buffer_[static_cast<std::size_t>(readIndex)];
    buffer_[static_cast<std::size_t>(writeIndex_)] = input;

    ++writeIndex_;
    if (writeIndex_ >= static_cast<int>(buffer_.size())) {
        writeIndex_ = 0;
    }

    return output;
}

} // namespace u273::dsp
