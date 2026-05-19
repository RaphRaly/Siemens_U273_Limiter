#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "u273/plugin/PluginProcessor.h"

namespace u273::plugin {

class PluginEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PluginEditor(U273AudioProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;

    U273AudioProcessor& processor_;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace u273::plugin
