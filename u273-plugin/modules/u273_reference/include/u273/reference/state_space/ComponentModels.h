#pragma once

#include <array>

namespace u273::reference::state_space {

inline constexpr double kRoomTemperatureThermalVoltage = 0.025852;

// Clamp exponential arguments in nonlinear device equations so Newton steps
// fail gracefully instead of overflowing to infinities.
[[nodiscard]] double limitedExp(double argument, double maxArgument = 40.0) noexcept;

enum class DiodeLaw {
    shockley = 0,
    u273EmpiricalComposite
};

// Shockley-style diode approximation used by offline state-space experiments.
struct DiodeModel {
    DiodeLaw law {DiodeLaw::shockley};
    double saturationCurrentAmp {1.0e-12};
    double ideality {};
    double thermalVoltage {kRoomTemperatureThermalVoltage};
    double gminSiemens {1.0e-12};
    double maxExpArgument {40.0};
    double reverseBreakdownVoltage {};
    double reverseBreakdownCurrentAmp {};
    double reverseBreakdownIdeality {6.0};
    double empiricalCurrentCoefficientMicroAmpPerMilliVolt {2.85e-16};
    double empiricalVoltageExponent {6.25};
    double empiricalMaxVoltage {1.2};

    [[nodiscard]] double currentAmp(double voltageAnodeCathode) const noexcept;
    [[nodiscard]] double conductanceSiemens(double voltageAnodeCathode) const noexcept;
    [[nodiscard]] bool hasReverseBreakdown() const noexcept;
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
[[nodiscard]] DiodeModel makeZl10Approximation() noexcept;
[[nodiscard]] DiodeModel makeOa154Approximation() noexcept;
[[nodiscard]] DiodeModel makeU273EmpiricalCompositeDiode() noexcept;
[[nodiscard]] NpnBjtModel makeSmallSignalNpnApproximation(double betaForward) noexcept;

} // namespace u273::reference::state_space
