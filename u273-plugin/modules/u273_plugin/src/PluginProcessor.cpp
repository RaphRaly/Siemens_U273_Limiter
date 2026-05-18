#include "u273/plugin/PluginProcessor.h"

#include <algorithm>

#include "u273/dsp/NominalReductionTable.h"
#include "u273/plugin/PluginEditor.h"

namespace u273::plugin {

U273AudioProcessor::U273AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters_(*this, nullptr, "U273State", ParameterBridge::buildParameterLayout())
    , parameterBridge_(parameters_)
    , presetManager_(parameters_)
{
    parameterBridge_.bind();
}

void U273AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    dspEngine_.prepare(u273::dsp::DspPrepareConfig {
        sampleRate,
        samplesPerBlock,
        std::max(1, std::min(getTotalNumOutputChannels(), u273::core::kMaxRealtimeChannels))});
    (void) dspEngine_.loadReductionTable(u273::dsp::makeNominalU273ReductionTable());
}

void U273AudioProcessor::releaseResources()
{
    dspEngine_.reset();
}

bool U273AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput) {
        return false;
    }

    return mainOutput == juce::AudioChannelSet::mono()
        || mainOutput == juce::AudioChannelSet::stereo();
}

void U273AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (auto channel = totalInputChannels; channel < totalOutputChannels; ++channel) {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    // Only channels present on both sides are processed; extra outputs are
    // cleared above to satisfy host layout contracts.
    const auto processChannels = std::min({totalInputChannels, totalOutputChannels, u273::core::kMaxRealtimeChannels});
    if (processChannels <= 0 || buffer.getNumSamples() <= 0) {
        return;
    }

    auto snapshot = parameterBridge_.readSnapshot(u273::core::kParameterSnapshotVersion);
    auto* writePointers = buffer.getArrayOfWritePointers();

    // From here the DSP sees only plain core types, never JUCE buffers or
    // parameter objects.
    const u273::core::ProcessContext context {
        u273::core::AudioBlockView {writePointers, processChannels, buffer.getNumSamples()},
        sampleRate_,
        blockSequence_++,
        true};

    u273::core::MeterFrame frame {};
    const auto status = dspEngine_.process(context, snapshot, &frame);

    if (status == u273::dsp::ProcessStatus::ok) {
        meterBridge_.publish(frame);
    }
}

juce::AudioProcessorEditor* U273AudioProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void U273AudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String U273AudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void U273AudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void U273AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    presetManager_.writeStateTo(destData);
}

void U273AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    presetManager_.restoreStateFrom(data, sizeInBytes);
}

} // namespace u273::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new u273::plugin::U273AudioProcessor();
}
