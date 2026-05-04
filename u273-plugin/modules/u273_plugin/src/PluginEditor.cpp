#include "u273/plugin/PluginEditor.h"

namespace u273::plugin {

PluginEditor::PluginEditor(U273AudioProcessor& processor)
    : juce::GenericAudioProcessorEditor(processor)
{
    setSize(420, 520);
}

} // namespace u273::plugin
