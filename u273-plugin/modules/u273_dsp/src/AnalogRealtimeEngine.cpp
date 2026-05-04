#include "u273/dsp/AnalogRealtimeEngine.h"

#include <algorithm>
#include <cmath>

#include "u273/core/Math.h"

namespace u273::dsp {

namespace {

constexpr auto kB6BridgeVoltageCoefficientMilliVolt = 308.0f;
constexpr auto kB6BridgeVoltageExponent = 0.16f;
constexpr auto kB6DynamicResistanceCoefficientOhm = 48300.0f;
constexpr auto kB6DynamicResistanceExponent = -0.84f;
constexpr auto kB6FeedResistanceOhm = 5600.0f;
constexpr auto kB6CurrentMinMicroAmp = 2.0f;
constexpr auto kB6CurrentMaxMicroAmp = 500.0f;
constexpr auto kMaxGainReductionDb = 36.0f;

[[nodiscard]] bool finitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0f;
}

} // namespace

bool AnalogRealtimeFrame::isValid() const noexcept
{
    return std::isfinite(commandVoltage)
        && commandVoltage >= 0.0f
        && std::isfinite(bridgeCurrentMicroAmp)
        && bridgeCurrentMicroAmp >= 0.0f
        && finitePositive(diodeDynamicResistanceOhm)
        && std::isfinite(gainReductionDb)
        && gainReductionDb >= 0.0f;
}

void AnalogRealtimeEngine::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    reset();
}

void AnalogRealtimeEngine::reset() noexcept
{
    lastFrame_ = AnalogRealtimeFrame {};
    lastFrame_.diodeDynamicResistanceOhm = 1.0e12f;
}

float AnalogRealtimeEngine::evaluateGainReductionDb(float detectorEnvelope,
                                                    const u273::core::ParameterSnapshot& snapshot) noexcept
{
    // Convert detector output to the bridge command domain, then use the
    // empirical current/resistance law to estimate attenuation.
    const auto commandVoltage = detectorToCommandVoltage(detectorEnvelope, snapshot);
    const auto commandMilliVolt = commandVoltage * 1000.0f;

    auto currentMicroAmp = 0.0f;
    if (commandMilliVolt > 0.0f) {
        currentMicroAmp = std::pow(commandMilliVolt / kB6BridgeVoltageCoefficientMilliVolt,
                                   1.0f / kB6BridgeVoltageExponent);
        currentMicroAmp = std::clamp(currentMicroAmp, 0.0f, kB6CurrentMaxMicroAmp);
    }

    const auto resistanceOhm = diodeDynamicResistanceOhm(currentMicroAmp);
    const auto bridgeResistanceOhm = std::max(1.0f, resistanceOhm);
    const auto voltageGain = bridgeResistanceOhm / (bridgeResistanceOhm + kB6FeedResistanceOhm);
    auto gainReductionDb = -20.0f * std::log10(std::clamp(voltageGain, 1.0e-6f, 1.0f));

    const auto calibrationTrim = u273::core::dbToLinear(snapshot.calibrationLevelDb * 0.1f);
    gainReductionDb *= calibrationTrim;
    gainReductionDb = std::clamp(gainReductionDb, 0.0f, kMaxGainReductionDb);

    lastFrame_.commandVoltage = commandVoltage;
    lastFrame_.bridgeCurrentMicroAmp = currentMicroAmp;
    lastFrame_.diodeDynamicResistanceOhm = resistanceOhm;
    lastFrame_.gainReductionDb = gainReductionDb;
    return gainReductionDb;
}

float AnalogRealtimeEngine::diodeDynamicResistanceOhm(float currentMicroAmp) noexcept
{
    // Below the documented useful current range the bridge is treated as open,
    // which avoids unstable gain jumps around silence.
    if (currentMicroAmp < kB6CurrentMinMicroAmp) {
        return 1.0e12f;
    }

    const auto boundedCurrent = std::clamp(currentMicroAmp, kB6CurrentMinMicroAmp, kB6CurrentMaxMicroAmp);
    return kB6DynamicResistanceCoefficientOhm
        * std::pow(boundedCurrent, kB6DynamicResistanceExponent);
}

float AnalogRealtimeEngine::detectorToCommandVoltage(float detectorEnvelope,
                                                     const u273::core::ParameterSnapshot& snapshot) noexcept
{
    if (!std::isfinite(detectorEnvelope) || detectorEnvelope <= 0.0f) {
        return 0.0f;
    }

    const auto drive = std::clamp(snapshot.drive, 0.0f, 1.0f);
    const auto commandScale = 0.25f + 0.75f * drive;
    return std::clamp(detectorEnvelope * commandScale, 0.0f, 3.0f);
}

} // namespace u273::dsp
