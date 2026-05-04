#pragma once

#include <array>

namespace u273::reference::state_space {

inline constexpr double kRoomTemperatureThermalVoltage = 0.025852;

// Clamp exponential arguments in nonlinear device equations so Newton steps
// fail gracefully instead of overflowing to infinities.
[[nodiscard]] double limitedExp(double argument, double maxArgument = 40.0) noexcept;

// Shockley-style diode approximation used by offline state-space experiments.
struct DiodeModel {
    double saturationCurrentAmp {1.0e-12};
    double ideality {};
    double thermalVoltage {kRoomTemperatureThermalVoltage};
    double gminSiemens {1.0e-12};
    double maxExpArgument {40.0};

    [[nodiscard]] double currentAmp(double voltageAnodeCathode) const noexcept;
    [[nodiscard]] double conductanceSiemens(double voltageAnodeCathode) const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
};

// Minimal Ebers-Moll NPN model for guarded active-device hypotheses.
struct NpnBjtModel {
    double saturationCurrentAmp {1.0e-15};
    double betaForward {120.0};
    double betaReverse {2.0};
    double thermalVoltage {kRoomTemperatureThermalVoltage};
    double gminSiemens {1.0e-12};
    double maxExpArgument {40.0};

    [[nodiscard]] bool isValid() const noexcept;
};

struct BjtEvaluation {
    // Terminal order: collector, base, emitter. Currents are positive leaving
    // the corresponding terminal into the device.
    std::array<double, 3> currentsAmp {};
    std::array<std::array<double, 3>, 3> dCurrentDVoltage {};
};

[[nodiscard]] BjtEvaluation evaluateNpnEbersMoll(const NpnBjtModel& model,
                                                 double collectorVolt,
                                                 double baseVolt,
                                                 double emitterVolt) noexcept;

[[nodiscard]] DiodeModel makeSsd55Approximation() noexcept;
[[nodiscard]] DiodeModel makeOa154Approximation() noexcept;
[[nodiscard]] NpnBjtModel makeSmallSignalNpnApproximation(double betaForward) noexcept;

} // namespace u273::reference::state_space
