#include "u273/plugin/PresetManager.h"

namespace u273::plugin {

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& state) noexcept
    : state_(&state)
{
}

void PresetManager::writeStateTo(juce::MemoryBlock& destination) const
{
    if (state_ == nullptr) {
        return;
    }

    const auto state = state_->copyState();
    const auto xml = state.createXml();
    if (xml != nullptr) {
        juce::AudioProcessor::copyXmlToBinary(*xml, destination);
    }
}

void PresetManager::restoreStateFrom(const void* data, int sizeInBytes)
{
    if (state_ == nullptr) {
        return;
    }

    const auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName(state_->state.getType())) {
        state_->replaceState(juce::ValueTree::fromXml(*xml));
    }
}

} // namespace u273::plugin
