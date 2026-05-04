#include "u273/plugin/ParameterBridge.h"

#include <memory>
#include <string>
#include <vector>

#include "u273/core/ParameterIds.h"

namespace u273::plugin {
namespace {

juce::String juceId(std::string_view id)
{
    return juce::String(std::string(id).c_str());
}

std::unique_ptr<juce::AudioParameterFloat> makeFloat(std::string_view id,
                                                     const juce::String& name,
                                                     const u273::core::ParameterRange& range)
{
    return std::make_unique<juce::AudioParameterFloat>(
        juceId(id),
        name,
        juce::NormalisableRange<float>(range.minimum, range.maximum),
        range.defaultValue);
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ParameterBridge::buildParameterLayout()
{
    // The order here is the host-visible parameter order; IDs come from core so
    // snapshots, automation, and saved state remain aligned.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.reserve(10);

    parameters.push_back(makeFloat(u273::core::param_id::inputGainDb, "Input", u273::core::ParameterRanges::inputGainDb));
    parameters.push_back(makeFloat(u273::core::param_id::outputGainDb, "Output", u273::core::ParameterRanges::outputGainDb));
    parameters.push_back(makeFloat(u273::core::param_id::drive, "Drive", u273::core::ParameterRanges::drive));
    parameters.push_back(makeFloat(u273::core::param_id::detectorScale, "Detector Scale", u273::core::ParameterRanges::detectorScale));
    parameters.push_back(makeFloat(u273::core::param_id::attackMs, "Attack", u273::core::ParameterRanges::attackMs));
    parameters.push_back(makeFloat(u273::core::param_id::releaseMs, "Release", u273::core::ParameterRanges::releaseMs));
    parameters.push_back(makeFloat(u273::core::param_id::mix, "Mix", u273::core::ParameterRanges::mix));
    parameters.push_back(makeFloat(u273::core::param_id::calibrationLevelDb, "Calibration", u273::core::ParameterRanges::calibrationLevelDb));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        juceId(u273::core::param_id::mode),
        "Mode",
        juce::StringArray {"Guarded Realtime", "Bypass"},
        0));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>(
        juceId(u273::core::param_id::bypass),
        "Bypass",
        false));

    return {parameters.begin(), parameters.end()};
}

ParameterBridge::ParameterBridge(juce::AudioProcessorValueTreeState& state) noexcept
    : state_(&state)
{
}

void ParameterBridge::bind() noexcept
{
    // Raw parameter pointers are cached once to keep processBlock free of
    // string lookups and APVTS traversal.
    inputGainDb_ = state_->getRawParameterValue(juceId(u273::core::param_id::inputGainDb));
    outputGainDb_ = state_->getRawParameterValue(juceId(u273::core::param_id::outputGainDb));
    mode_ = state_->getRawParameterValue(juceId(u273::core::param_id::mode));
    drive_ = state_->getRawParameterValue(juceId(u273::core::param_id::drive));
    detectorScale_ = state_->getRawParameterValue(juceId(u273::core::param_id::detectorScale));
    attackMs_ = state_->getRawParameterValue(juceId(u273::core::param_id::attackMs));
    releaseMs_ = state_->getRawParameterValue(juceId(u273::core::param_id::releaseMs));
    mix_ = state_->getRawParameterValue(juceId(u273::core::param_id::mix));
    calibrationLevelDb_ = state_->getRawParameterValue(juceId(u273::core::param_id::calibrationLevelDb));
    bypass_ = state_->getRawParameterValue(juceId(u273::core::param_id::bypass));
}

u273::core::ParameterSnapshot ParameterBridge::readSnapshot(std::uint32_t version) const noexcept
{
    u273::core::ParameterSnapshot snapshot {};
    snapshot.version = version;
    snapshot.inputGainDb = read(inputGainDb_, u273::core::ParameterRanges::inputGainDb.defaultValue);
    snapshot.outputGainDb = read(outputGainDb_, u273::core::ParameterRanges::outputGainDb.defaultValue);
    snapshot.drive = read(drive_, u273::core::ParameterRanges::drive.defaultValue);
    snapshot.detectorScale = read(detectorScale_, u273::core::ParameterRanges::detectorScale.defaultValue);
    snapshot.attackMs = read(attackMs_, u273::core::ParameterRanges::attackMs.defaultValue);
    snapshot.releaseMs = read(releaseMs_, u273::core::ParameterRanges::releaseMs.defaultValue);
    snapshot.mix = read(mix_, u273::core::ParameterRanges::mix.defaultValue);
    snapshot.calibrationLevelDb = read(calibrationLevelDb_, u273::core::ParameterRanges::calibrationLevelDb.defaultValue);
    snapshot.mode = read(mode_, 0.0f) >= 0.5f ? u273::core::U273Mode::bypass : u273::core::U273Mode::guardedRealtime;
    snapshot.bypass = read(bypass_, 0.0f) >= 0.5f;
    snapshot.modelBoundary = u273::core::currentScientificBoundary();
    return snapshot;
}

float ParameterBridge::read(std::atomic<float>* value, float fallback) noexcept
{
    return value != nullptr ? value->load(std::memory_order_relaxed) : fallback;
}

} // namespace u273::plugin
