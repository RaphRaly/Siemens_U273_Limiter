#pragma once

#include <array>

namespace u273::dsp {

class LinearPhaseFirResampler {
public:
    static constexpr int kMaxOversamplingFactor = 16;
    static constexpr int kHostLatencySamples = 16;
    static constexpr int kMaxTaps = kHostLatencySamples * kMaxOversamplingFactor + 1;

    [[nodiscard]] static bool isSupportedFactor(int factor) noexcept;

    [[nodiscard]] bool prepare(int oversamplingFactor) noexcept;
    void reset() noexcept;

    [[nodiscard]] float process(float input, float gain) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] int oversamplingFactor() const noexcept { return oversamplingFactor_; }
    [[nodiscard]] int latencySamples() const noexcept
    {
        return oversamplingFactor_ > 1 ? kHostLatencySamples : 0;
    }
    [[nodiscard]] double latencySamplesExact() const noexcept
    {
        return static_cast<double>(latencySamples());
    }
    [[nodiscard]] int tapCount() const noexcept { return tapCount_; }

private:
    [[nodiscard]] float processFir(std::array<float, kMaxTaps>& history,
                                   int& writeIndex,
                                   float input) const noexcept;
    void buildKernel() noexcept;

    int oversamplingFactor_ {1};
    int tapCount_ {1};
    bool prepared_ {false};
    std::array<float, kMaxTaps> coefficients_ {};
    std::array<float, kMaxTaps> upHistory_ {};
    std::array<float, kMaxTaps> downHistory_ {};
    int upWriteIndex_ {};
    int downWriteIndex_ {};
};

} // namespace u273::dsp
