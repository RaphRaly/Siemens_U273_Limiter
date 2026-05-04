#pragma once

namespace u273::dsp {

// Attack/release envelope follower used to turn the sidechain peak into a
// smooth control signal for the analog gain-reduction model.
class DetectorEnvelope {
public:
    void prepare(double sampleRate) noexcept;
    void reset(float value = 0.0f) noexcept;
    void setTimeConstants(float attackMs, float releaseMs) noexcept;

    [[nodiscard]] float processSample(float inputAbs) noexcept;
    [[nodiscard]] float value() const noexcept { return envelope_; }

private:
    [[nodiscard]] static float coefficient(double sampleRate, float timeMs) noexcept;

    double sampleRate_ {48000.0};
    float attackCoefficient_ {0.0f};
    float releaseCoefficient_ {0.0f};
    float envelope_ {0.0f};
};

} // namespace u273::dsp
