#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace u273::plugin {

// Thin persistence wrapper around APVTS XML state. Keeping this separate keeps
// PluginProcessor focused on host callbacks instead of serialization details.
class PresetManager {
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& state) noexcept;

    void writeStateTo(juce::MemoryBlock& destination) const;
    void restoreStateFrom(const void* data, int sizeInBytes);

private:
    juce::AudioProcessorValueTreeState* state_ {};
};

} // namespace u273::plugin
