#include "u273/dsp/LinearPhaseFirResampler.h"

#include <algorithm>
#include <cmath>

namespace u273::dsp {

namespace {

constexpr double kPi = 3.1415926535897932384626433832795;

[[nodiscard]] double sinc(double value) noexcept
{
    if (std::fabs(value) <= 1.0e-12) {
        return 1.0;
    }

    return std::sin(kPi * value) / (kPi * value);
}

} // namespace

bool LinearPhaseFirResampler::isSupportedFactor(int factor) noexcept
{
    return factor == 1
        || factor == 2
        || factor == 4
        || factor == 8
        || factor == 16;
}

bool LinearPhaseFirResampler::prepare(int oversamplingFactor) noexcept
{
    prepared_ = false;
    oversamplingFactor_ = 1;
    tapCount_ = 1;
    coefficients_.fill(0.0f);
    coefficients_[0] = 1.0f;
    reset();

    if (!isSupportedFactor(oversamplingFactor)) {
        return false;
    }

    oversamplingFactor_ = std::max(1, oversamplingFactor);
    tapCount_ = oversamplingFactor_ > 1
        ? kHostLatencySamples * oversamplingFactor_ + 1
        : 1;

    if (tapCount_ > kMaxTaps) {
        oversamplingFactor_ = 1;
        tapCount_ = 1;
        coefficients_[0] = 1.0f;
        reset();
        return false;
    }

    buildKernel();
    reset();
    prepared_ = true;
    return true;
}

void LinearPhaseFirResampler::reset() noexcept
{
    upHistory_.fill(0.0f);
    downHistory_.fill(0.0f);
    upWriteIndex_ = 0;
    downWriteIndex_ = 0;
}

float LinearPhaseFirResampler::process(float input, float gain) noexcept
{
    if (!prepared_) {
        return input * gain;
    }

    if (oversamplingFactor_ <= 1) {
        return input * gain;
    }

    auto decimated = 0.0f;
    for (auto phase = 0; phase < oversamplingFactor_; ++phase) {
        const auto injected = phase == 0 ? input : 0.0f;
        const auto reconstructed = processFir(upHistory_, upWriteIndex_, injected)
            * static_cast<float>(oversamplingFactor_);
        const auto processed = reconstructed * gain;
        const auto filtered = processFir(downHistory_, downWriteIndex_, processed);
        if (phase == 0) {
            decimated = filtered;
        }
    }

    return decimated;
}

float LinearPhaseFirResampler::processFir(std::array<float, kMaxTaps>& history,
                                          int& writeIndex,
                                          float input) const noexcept
{
    history[static_cast<std::size_t>(writeIndex)] = input;

    auto output = 0.0f;
    auto readIndex = writeIndex;
    for (auto tap = 0; tap < tapCount_; ++tap) {
        output += coefficients_[static_cast<std::size_t>(tap)]
            * history[static_cast<std::size_t>(readIndex)];
        --readIndex;
        if (readIndex < 0) {
            readIndex = tapCount_ - 1;
        }
    }

    ++writeIndex;
    if (writeIndex >= tapCount_) {
        writeIndex = 0;
    }

    return output;
}

void LinearPhaseFirResampler::buildKernel() noexcept
{
    coefficients_.fill(0.0f);

    if (oversamplingFactor_ <= 1 || tapCount_ <= 1) {
        coefficients_[0] = 1.0f;
        return;
    }

    const auto center = static_cast<double>(tapCount_ - 1) * 0.5;
    const auto cutoffCycles = 0.475 / static_cast<double>(oversamplingFactor_);
    auto sum = 0.0;

    for (auto tap = 0; tap < tapCount_; ++tap) {
        const auto n = static_cast<double>(tap);
        const auto offset = n - center;
        const auto ideal = 2.0 * cutoffCycles * sinc(2.0 * cutoffCycles * offset);
        const auto denominator = static_cast<double>(tapCount_ - 1);
        const auto window = 0.42
            - 0.5 * std::cos(2.0 * kPi * n / denominator)
            + 0.08 * std::cos(4.0 * kPi * n / denominator);
        const auto value = ideal * window;
        coefficients_[static_cast<std::size_t>(tap)] = static_cast<float>(value);
        sum += value;
    }

    if (std::fabs(sum) <= 1.0e-20) {
        coefficients_.fill(0.0f);
        coefficients_[static_cast<std::size_t>(tapCount_ / 2)] = 1.0f;
        return;
    }

    const auto scale = 1.0 / sum;
    for (auto tap = 0; tap < tapCount_; ++tap) {
        coefficients_[static_cast<std::size_t>(tap)] =
            static_cast<float>(static_cast<double>(coefficients_[static_cast<std::size_t>(tap)]) * scale);
    }
}

} // namespace u273::dsp
