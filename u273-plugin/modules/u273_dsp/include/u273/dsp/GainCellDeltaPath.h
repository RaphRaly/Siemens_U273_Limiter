#pragma once

#include <array>

#include "u273/core/Constants.h"
#include "u273/dsp/LinearPhaseFirResampler.h"

namespace u273::dsp {

class GainCellDeltaPath {
public:
    [[nodiscard]] bool prepare(int oversamplingFactor, int numChannels) noexcept;
    void reset() noexcept;

    [[nodiscard]] float processSample(int channel, float dry, float wetGain, float mix) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] int oversamplingFactor() const noexcept { return oversamplingFactor_; }
    [[nodiscard]] int latencySamples() const noexcept
    {
        return oversamplingFactor_ > 1 ? LinearPhaseFirResampler::kHostLatencySamples : 0;
    }
    [[nodiscard]] double latencySamplesExact() const noexcept
    {
        return static_cast<double>(latencySamples());
    }

private:
    class DryDelay {
    public:
        void prepare(int delaySamples) noexcept;
        void reset() noexcept;
        [[nodiscard]] float process(float input) noexcept;

    private:
        static constexpr int kMaxDelaySamples = LinearPhaseFirResampler::kHostLatencySamples;
        std::array<float, kMaxDelaySamples + 1> buffer_ {};
        int delaySamples_ {};
        int writeIndex_ {};
    };

    struct ChannelState {
        LinearPhaseFirResampler reference {};
        LinearPhaseFirResampler processed {};
        DryDelay dryDelay {};
    };

    std::array<ChannelState, u273::core::kMaxRealtimeChannels> channels_ {};
    int oversamplingFactor_ {1};
    int numChannels_ {};
    bool prepared_ {false};
};

} // namespace u273::dsp
