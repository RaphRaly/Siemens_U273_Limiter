#include "u273/reference/calibration/ActiveModelParameterMapping.h"

#include <algorithm>
#include <cmath>

namespace u273::reference::calibration {

namespace {

constexpr double kSafeExpClampHi = 60.0;
constexpr double kSafeExpClampLo = -60.0;

[[nodiscard]] double safeExp(double argument)
{
    const auto clamped = std::clamp(argument, kSafeExpClampLo, kSafeExpClampHi);
    return std::exp(clamped);
}

[[nodiscard]] double empiricalExponent(const ActiveModelParameters& parameters) noexcept
{
    const auto b = parameters.diodeB.value;
    return b > 0.0 && std::isfinite(b) ? 1.0 / b : 0.0;
}

[[nodiscard]] double empiricalCoefficientMicroAmpPerMilliVolt(
    const ActiveModelParameters& parameters) noexcept
{
    const auto a = parameters.diodeA.value;
    const auto exponent = empiricalExponent(parameters);
    if (!(a > 0.0) || !(exponent > 0.0) || !std::isfinite(a) || !std::isfinite(exponent)) {
        return 0.0;
    }
    return std::pow(1.0 / a, exponent);
}

} // namespace

u273::reference::state_space::CircuitGraph applyActiveModelParametersToCircuit(
    u273::reference::state_space::CircuitGraph circuit,
    const ActiveModelParameters& parameters)
{
    const auto saturationCurrent = safeExp(parameters.logIs.value);
    const auto empiricalCoefficient = empiricalCoefficientMicroAmpPerMilliVolt(parameters);
    const auto exponent = empiricalExponent(parameters);

    for (auto& diode : circuit.mutableDiodes()) {
        diode.model.gminSiemens = parameters.numericalGmin.value;
        if (diode.model.law == u273::reference::state_space::DiodeLaw::u273EmpiricalComposite) {
            diode.model.empiricalCurrentCoefficientMicroAmpPerMilliVolt = empiricalCoefficient;
            diode.model.empiricalVoltageExponent = exponent;
        } else {
            diode.model.saturationCurrentAmp = saturationCurrent;
        }
    }

    for (auto& bjt : circuit.mutableNpnBjts()) {
        bjt.model.saturationCurrentAmp = saturationCurrent;
        bjt.model.betaForward = parameters.betaForward.value;
        bjt.model.betaReverse = parameters.betaReverse.value;
        bjt.model.gminSiemens = parameters.numericalGmin.value;
    }

    return circuit;
}

} // namespace u273::reference::calibration
