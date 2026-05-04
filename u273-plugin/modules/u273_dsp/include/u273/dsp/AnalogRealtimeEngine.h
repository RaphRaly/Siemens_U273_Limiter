#pragma once

#include "u273/dsp/RealtimeGainReductionModel.h"

namespace u273::dsp {

// Per-sample telemetry from the analog bridge approximation. It is intentionally
// small so tests can validate model health without coupling to implementation.
struct AnalogRealtimeFrame {
    float commandVoltage {};
    float bridgeCurrentMicroAmp {};
    float diodeDynamicResistanceOhm {};
    float gainReductionDb {};

    [[nodiscard]] bool isValid() const noexcept;
};

// Realtime-safe diode bridge approximation. It avoids allocation and external
// dependencies so it can run directly from the audio callback.
class AnalogRealtimeEngine final : public RealtimeGainReductionModel {
public:
    void prepare(double sampleRate) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] float evaluateGainReductionDb(float detectorEnvelope,
                                                const u273::core::ParameterSnapshot& snapshot) noexcept override;
    [[nodiscard]] const AnalogRealtimeFrame& lastFrame() const noexcept { return lastFrame_; }

    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept override
    {
        return u273::core::ModelBoundary::fullActiveModelUnverified;
    }

private:
    [[nodiscard]] static float diodeDynamicResistanceOhm(float currentMicroAmp) noexcept;
    [[nodiscard]] static float detectorToCommandVoltage(float detectorEnvelope,
                                                        const u273::core::ParameterSnapshot& snapshot) noexcept;

    double sampleRate_ {48000.0};
    AnalogRealtimeFrame lastFrame_ {};
};

} // namespace u273::dsp
