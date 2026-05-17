#pragma once

#include <array>
#include <cstddef>

#include "u273/dsp/RealtimeGainReductionModel.h"

namespace u273::dsp {

// Telemetry frame produced per evaluation by the full active realtime engine.
// Kept intentionally small so tests can introspect engine state without coupling
// to internal data structures. In wave B1 the DSP fields stay at zero; wave B2
// will populate them once Newton + integration land.
struct FullActiveRealtimeFrame {
    float gainReductionDb {};
    float capacitorVoltage0 {};
    float capacitorVoltage1 {};
    int xrunCount {};
    int sampleCount {};

    [[nodiscard]] bool isValid() const noexcept;
};

// Numerical integration method used by the realtime Newton inner loop. Wave B2
// only implements backwardEuler; trBdf2 is reserved for wave B3 where the
// implicit-explicit pair will tighten convergence on stiff diode-cap nodes.
enum class IntegrationMethod {
    backwardEuler = 0,
    trBdf2 = 1
};

// Realtime-safe scaffold for the full active analog model. This skeleton holds
// the calibrated parameters and per-sample state buffers required by the future
// Newton solver; the actual DSP arrives in wave B2. The class is allocation-free
// after construction. Accepting parameters arms the experimental solver but
// does not escalate boundary(); promotion is blocked until the strict
// device-output audio gate exists.
//
// Dependency note: we deliberately accept calibrated parameters as a plain
// std::array<double, 8> (canonical order: diodeA, diodeB, logIs, betaForward,
// betaReverse, earlyVoltage, detectorToCmdScale, numericalGmin) rather than the
// u273::reference::calibration::ActiveModelParameters struct. This keeps
// u273_dsp from depending transitively on u273_reference and matches the
// realtime contract (POD-only, no exceptions, no allocations).
class FullActiveRealtimeEngine final : public RealtimeGainReductionModel {
public:
    static constexpr std::size_t kMaxCapacitors = 8;
    static constexpr std::size_t kMaxUnknowns = 16;
    static constexpr std::size_t kCalibratedParameterCount = 8;

    // Newton inner-loop limits. Kept very tight (4 iterations) to bound the
    // worst-case per-sample cost in the audio thread; the diode-cap node is
    // smooth enough that this is plenty when warm-starting from the previous
    // unknown vector.
    static constexpr int kMaxNewtonIterations = 4;
    static constexpr double kPivotFloor = 1.0e-18;
    static constexpr double kDeltaTolerance = 1.0e-9;
    static constexpr double kThermalVoltage = 0.026;        // V, room temperature
    static constexpr double kCapacitanceFarad = 1.0e-6;     // 1 uF, RC ~ 1 ms with diode Rd
    static constexpr double kExpClampHigh = 60.0;
    static constexpr double kDeltaClamp = 2.0;
    static constexpr double kMinGainReductionDb = 0.0;
    static constexpr double kMaxGainReductionDb = 60.0;
    static constexpr double kDefaultIs = 1.0e-12;

    FullActiveRealtimeEngine() noexcept;

    void prepare(double sampleRate) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] float evaluateGainReductionDb(float detectorEnvelope,
                                                const u273::core::ParameterSnapshot& snapshot) noexcept override;

    // Called off the audio thread after runOffline() has produced calibrated
    // parameters. Values are copied by value into the engine; once this returns
    // with parametersSet_ == true, the engine is allocation-free for the audio
    // thread. Input is validated as finite and within plausible bounds; on
    // invalid input the engine keeps its previous (or initial) state.
    void setActiveModelParameters(const std::array<double, kCalibratedParameterCount>& parameters) noexcept;

    void setIntegrationMethod(IntegrationMethod method) noexcept;
    [[nodiscard]] IntegrationMethod integrationMethod() const noexcept { return integrationMethod_; }

    [[nodiscard]] int xrunCount() const noexcept { return xrunCount_; }

    [[nodiscard]] const FullActiveRealtimeFrame& lastFrame() const noexcept { return lastFrame_; }
    [[nodiscard]] bool hasParameters() const noexcept { return parametersSet_; }

    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept override
    {
        return u273::core::ModelBoundary::fullActiveModelUnverified;
    }

private:
    double sampleRate_ {48000.0};
    double samplePeriod_ {1.0 / 48000.0};
    double capacitorConductance_ {kCapacitanceFarad * 48000.0};  // gc = C / h
    bool parametersSet_ {false};
    IntegrationMethod integrationMethod_ {IntegrationMethod::backwardEuler};
    std::array<double, kCalibratedParameterCount> calibratedParameters_ {};

    std::array<double, kMaxCapacitors> capacitorVoltages_ {};
    std::array<double, kMaxCapacitors> capacitorCurrents_ {};
    std::array<double, kMaxUnknowns> previousUnknowns_ {};

    FullActiveRealtimeFrame lastFrame_ {};
    int xrunCount_ {0};
    int sampleCount_ {0};
};

} // namespace u273::dsp
