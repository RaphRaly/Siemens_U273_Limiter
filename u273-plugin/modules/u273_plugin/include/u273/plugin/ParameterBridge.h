#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

#include "u273/core/ParameterSnapshot.h"

namespace u273::plugin {

// Host-parameter adapter. It owns no parameter values; it caches raw APVTS
// atomics and emits a plain ParameterSnapshot for the DSP boundary.
class ParameterBridge {
public:
    static juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout();

    explicit ParameterBridge(juce::AudioProcessorValueTreeState& state) noexcept;

    void bind() noexcept;

    [[nodiscard]] u273::core::ParameterSnapshot readSnapshot(std::uint32_t version) const noexcept;

private:
    [[nodiscard]] static float read(std::atomic<float>* value, float fallback) noexcept;

    juce::AudioProcessorValueTreeState* state_ {};
    std::atomic<float>* inputGainDb_ {};
    std::atomic<float>* outputGainDb_ {};
    std::atomic<float>* mode_ {};
    std::atomic<float>* drive_ {};
    std::atomic<float>* detectorScale_ {};
    std::atomic<float>* attackMs_ {};
    std::atomic<float>* releaseMs_ {};
    std::atomic<float>* mix_ {};
    std::atomic<float>* calibrationLevelDb_ {};
    std::atomic<float>* bypass_ {};
};

} // namespace u273::plugin
