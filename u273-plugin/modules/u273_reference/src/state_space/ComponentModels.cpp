#include "u273/reference/state_space/ComponentModels.h"

#include <algorithm>
#include <cmath>

namespace u273::reference::state_space {

double limitedExp(double argument, double maxArgument) noexcept
{
    const auto limited = std::clamp(argument, -maxArgument, maxArgument);
    return std::exp(limited);
}

bool DiodeModel::isValid() const noexcept
{
    return saturationCurrentAmp > 0.0
        && ideality > 0.0
        && thermalVoltage > 0.0
        && gminSiemens >= 0.0
        && maxExpArgument > 1.0;
}

double DiodeModel::currentAmp(double voltageAnodeCathode) const noexcept
{
    if (!isValid()) {
        return 0.0;
    }

    const auto exponent = voltageAnodeCathode / (ideality * thermalVoltage);
    return saturationCurrentAmp * (limitedExp(exponent, maxExpArgument) - 1.0)
        + gminSiemens * voltageAnodeCathode;
}

double DiodeModel::conductanceSiemens(double voltageAnodeCathode) const noexcept
{
    if (!isValid()) {
        return gminSiemens;
    }

    const auto exponent = voltageAnodeCathode / (ideality * thermalVoltage);
    return saturationCurrentAmp * limitedExp(exponent, maxExpArgument) / (ideality * thermalVoltage)
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

DiodeModel makeOa154Approximation() noexcept
{
    DiodeModel model {};
    model.saturationCurrentAmp = 8.0e-9;
    model.ideality = 1.35;
    model.gminSiemens = 1.0e-12;
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
