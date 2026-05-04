#pragma once

#include "u273/core/Constants.h"

namespace u273::core {

// Non-owning audio buffer view used at the DSP boundary. It keeps JUCE types
// outside u273_dsp while still allowing in-place sample access.
struct AudioBlockView {
    float* const* channels {};
    int numChannels {};
    int numSamples {};

    [[nodiscard]] bool isValid() const noexcept
    {
        // Zero-sample blocks are valid, but non-empty channels must point to
        // writable audio memory supplied by the host adapter.
        if (channels == nullptr || numChannels <= 0 || numChannels > kMaxRealtimeChannels || numSamples < 0) {
            return false;
        }

        for (int channel = 0; channel < numChannels; ++channel) {
            if (channels[channel] == nullptr && numSamples > 0) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] float getSample(int channel, int sample) const noexcept
    {
        return channels[channel][sample];
    }

    void setSample(int channel, int sample, float value) const noexcept
    {
        channels[channel][sample] = value;
    }
};

} // namespace u273::core
