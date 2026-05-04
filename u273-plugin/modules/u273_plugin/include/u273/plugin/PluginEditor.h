#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "u273/plugin/PluginProcessor.h"

namespace u273::plugin {

// Placeholder editor using JUCE's generic parameter UI until the custom U273
// interface is designed.
class PluginEditor final : public juce::GenericAudioProcessorEditor {
public:
    explicit PluginEditor(U273AudioProcessor& processor);
};

} // namespace u273::plugin
