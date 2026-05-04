#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "u273/dsp/U273DspEngine.h"
#include "u273/plugin/MeterBridge.h"
#include "u273/plugin/ParameterBridge.h"
#include "u273/plugin/PresetManager.h"

namespace u273::plugin {

// JUCE host adapter. It owns framework state and delegates all audio math to
// u273_dsp so the realtime core remains framework-independent.
class U273AudioProcessor final : public juce::AudioProcessor {
public:
    U273AudioProcessor();
    ~U273AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "U273 Limiter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    [[nodiscard]] MeterBridge& meterBridge() noexcept { return meterBridge_; }
    [[nodiscard]] const MeterBridge& meterBridge() const noexcept { return meterBridge_; }

private:
    juce::AudioProcessorValueTreeState parameters_;
    ParameterBridge parameterBridge_;
    PresetManager presetManager_;
    MeterBridge meterBridge_ {};
    u273::dsp::U273DspEngine dspEngine_ {};
    double sampleRate_ {u273::core::kDefaultSampleRate};
    std::uint64_t blockSequence_ {};
};

} // namespace u273::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
