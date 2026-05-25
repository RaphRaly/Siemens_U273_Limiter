#include "u273/reference/state_space/ComponentModels.h"

#include <algorithm>
#include <cmath>

namespace u273::reference::state_space {

namespace {

[[nodiscard]] double reverseBreakdownCurrent(const DiodeModel& model, double voltageAnodeCathode) noexcept
{
    if (!model.hasReverseBreakdown() || voltageAnodeCathode >= -model.reverseBreakdownVoltage) {
        return 0.0;
    }

    const auto exponent = (-voltageAnodeCathode - model.reverseBreakdownVoltage)
        / (model.reverseBreakdownIdeality * model.thermalVoltage);
    return -model.reverseBreakdownCurrentAmp * (limitedExp(exponent, model.maxExpArgument) - 1.0);
}

[[nodiscard]] double reverseBreakdownConductance(const DiodeModel& model, double voltageAnodeCathode) noexcept
{
    if (!model.hasReverseBreakdown() || voltageAnodeCathode >= -model.reverseBreakdownVoltage) {
        return 0.0;
    }

    const auto exponent = (-voltageAnodeCathode - model.reverseBreakdownVoltage)
        / (model.reverseBreakdownIdeality * model.thermalVoltage);
    return model.reverseBreakdownCurrentAmp
        * limitedExp(exponent, model.maxExpArgument)
        / (model.reverseBreakdownIdeality * model.thermalVoltage);
}

} // namespace

double limitedExp(double argument, double maxArgument) noexcept
{
    const auto limited = std::clamp(argument, -maxArgument, maxArgument);
    return std::exp(limited);
}

bool DiodeModel::isValid() const noexcept
{
    if (law == DiodeLaw::u273EmpiricalComposite) {
        return empiricalCurrentCoefficientMicroAmpPerMilliVolt > 0.0
            && empiricalVoltageExponent > 1.0
            && empiricalMaxVoltage > 0.0
            && gminSiemens >= 0.0;
    }

    return saturationCurrentAmp > 0.0
        && ideality > 0.0
        && thermalVoltage > 0.0
        && gminSiemens >= 0.0
        && maxExpArgument > 1.0
        && (reverseBreakdownVoltage <= 0.0
            || (reverseBreakdownCurrentAmp > 0.0 && reverseBreakdownIdeality > 0.0));
}

bool DiodeModel::hasReverseBreakdown() const noexcept
{
    return reverseBreakdownVoltage > 0.0
        && reverseBreakdownCurrentAmp > 0.0
        && reverseBreakdownIdeality > 0.0;
}

double DiodeModel::currentAmp(double voltageAnodeCathode) const noexcept
{
    if (!isValid()) {
        return 0.0;
    }

    if (law == DiodeLaw::u273EmpiricalComposite) {
        if (voltageAnodeCathode <= 0.0) {
            return gminSiemens * voltageAnodeCathode;
        }

        const auto voltageVolt = std::min(voltageAnodeCathode, empiricalMaxVoltage);
        const auto voltageMilliVolt = voltageVolt * 1000.0;
        const auto currentMicroAmp = empiricalCurrentCoefficientMicroAmpPerMilliVolt
            * std::pow(voltageMilliVolt, empiricalVoltageExponent);
        return currentMicroAmp * 1.0e-6 + gminSiemens * voltageAnodeCathode;
    }

    const auto exponent = voltageAnodeCathode / (ideality * thermalVoltage);
    return saturationCurrentAmp * (limitedExp(exponent, maxExpArgument) - 1.0)
        + reverseBreakdownCurrent(*this, voltageAnodeCathode)
        + gminSiemens * voltageAnodeCathode;
}

double DiodeModel::conductanceSiemens(double voltageAnodeCathode) const noexcept
{
    if (!isValid()) {
        return gminSiemens;
    }

    if (law == DiodeLaw::u273EmpiricalComposite) {
        if (voltageAnodeCathode <= 0.0 || voltageAnodeCathode > empiricalMaxVoltage) {
            return gminSiemens;
        }

        const auto voltageMilliVolt = voltageAnodeCathode * 1000.0;
        return empiricalCurrentCoefficientMicroAmpPerMilliVolt
            * empiricalVoltageExponent
            * std::pow(voltageMilliVolt, empiricalVoltageExponent - 1.0)
            * 1.0e-3
            + gminSiemens;
    }

    const auto exponent = voltageAnodeCathode / (ideality * thermalVoltage);
    return saturationCurrentAmp * limitedExp(exponent, maxExpArgument) / (ideality * thermalVoltage)
        + reverseBreakdownConductance(*this, voltageAnodeCathode)
        + gminSiemens;
}

bool NpnBjtModel::isValid() const noexcept
{
    return saturationCurrentAmp > 0.0
        && betaForward > 0.0
        && betaReverse > 0.0
        && thermalVoltage > 0.0
        && gminSiemens >= 0.0
        && maxExpArgument > 1.0;
}

BjtEvaluation evaluateNpnEbersMoll(const NpnBjtModel& model,
                                   double collectorVolt,
                                   double baseVolt,
                                   double emitterVolt) noexcept
{
    BjtEvaluation evaluation {};
    if (!model.isValid()) {
        return evaluation;
    }

    const auto alphaForward = model.betaForward / (model.betaForward + 1.0);
    const auto alphaReverse = model.betaReverse / (model.betaReverse + 1.0);
    const auto vbe = baseVolt - emitterVolt;
    const auto vbc = baseVolt - collectorVolt;
    const auto expForward = limitedExp(vbe / model.thermalVoltage, model.maxExpArgument);
    const auto expReverse = limitedExp(vbc / model.thermalVoltage, model.maxExpArgument);
    const auto forwardCurrent = model.saturationCurrentAmp * (expForward - 1.0);
    const auto reverseCurrent = model.saturationCurrentAmp * (expReverse - 1.0);
    const auto dForward = model.saturationCurrentAmp * expForward / model.thermalVoltage;
    const auto dReverse = model.saturationCurrentAmp * expReverse / model.thermalVoltage;

    const auto collectorCurrent = alphaForward * forwardCurrent
        - reverseCurrent
        + model.gminSiemens * (collectorVolt - emitterVolt);
    const auto emitterCurrent = -forwardCurrent
        + alphaReverse * reverseCurrent
        + model.gminSiemens * (emitterVolt - collectorVolt);
    const auto baseCurrent = -collectorCurrent - emitterCurrent;

    evaluation.currentsAmp = {collectorCurrent, baseCurrent, emitterCurrent};

    auto& jacobian = evaluation.dCurrentDVoltage;

    jacobian[0][0] = dReverse + model.gminSiemens;
    jacobian[0][1] = alphaForward * dForward - dReverse;
    jacobian[0][2] = -alphaForward * dForward - model.gminSiemens;

    jacobian[2][0] = -alphaReverse * dReverse - model.gminSiemens;
    jacobian[2][1] = -dForward + alphaReverse * dReverse;
    jacobian[2][2] = dForward + model.gminSiemens;

    for (std::size_t column = 0; column < 3; ++column) {
        jacobian[1][column] = -jacobian[0][column] - jacobian[2][column];
    }

    return evaluation;
}

DiodeModel makeSsd55Approximation() noexcept
{
    DiodeModel model {};
    model.saturationCurrentAmp = 2.5e-12;
    model.ideality = 1.85;
    model.gminSiemens = 1.0e-12;
    return model;
}

DiodeModel makeZl10Approximation() noexcept
{
    DiodeModel model {};
    model.saturationCurrentAmp = 1.0e-12;
    model.ideality = 2.0;
    model.reverseBreakdownVoltage = 10.0;
    model.reverseBreakdownCurrentAmp = 1.0e-6;
    model.reverseBreakdownIdeality = 6.0;
    model.gminSiemens = 1.0e-12;
    return model;
}

DiodeModel makeOa154Approximation() noexcept
{
    DiodeModel model {};
    model.saturationCurrentAmp = 8.0e-9;
    model.ideality = 1.35;
    model.gminSiemens = 1.0e-12;
    return model;
}

DiodeModel makeU273EmpiricalCompositeDiode() noexcept
{
    DiodeModel model {};
    model.law = DiodeLaw::u273EmpiricalComposite;
    model.gminSiemens = 1.0e-12;
    model.empiricalCurrentCoefficientMicroAmpPerMilliVolt = 2.85e-16;
    model.empiricalVoltageExponent = 6.25;
    model.empiricalMaxVoltage = 1.2;
    return model;
}

NpnBjtModel makeSmallSignalNpnApproximation(double betaForward) noexcept
{
    NpnBjtModel model {};
    model.saturationCurrentAmp = 1.0e-15;
    model.betaForward = betaForward;
    model.betaReverse = 2.0;
    model.gminSiemens = 1.0e-12;
    return model;
}

} // namespace u273::reference::state_space
