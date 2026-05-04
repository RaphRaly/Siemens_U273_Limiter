#include "u273/reference/ScientificReferenceModel.h"

#include "u273/core/Math.h"

namespace u273::reference {

bool ReferenceScenario::isValid() const noexcept
{
    return u273::core::isFinite(inputLevelDbu)
        && u273::core::isFinite(frequencyHz)
        && u273::core::isFinite(durationSeconds)
        && frequencyHz > 0.0
        && durationSeconds >= 0.0
        && parameters.isValid();
}

bool ReferenceSolveResult::isValid() const noexcept
{
    return u273::core::isFinite(residualNorm)
        && u273::core::isFinite(outputLevelDbu)
        && iterations >= 0
        && boundary != u273::core::ModelBoundary::unknown;
}

ReferenceSolveResult ScientificReferenceModel::solveDc(const ReferenceScenario& scenario) const noexcept
{
    ReferenceSolveResult result {};
    result.converged = scenario.isValid();
    result.iterations = result.converged ? 1 : 0;
    result.outputLevelDbu = scenario.inputLevelDbu;
    return result;
}

ReferenceSolveResult ScientificReferenceModel::solveAc(const ReferenceScenario& scenario) const noexcept
{
    ReferenceSolveResult result {};
    result.converged = scenario.isValid();
    result.iterations = result.converged ? 1 : 0;
    result.outputLevelDbu = scenario.inputLevelDbu;
    return result;
}

ReferenceSolveResult ScientificReferenceModel::solveTransient(const ReferenceScenario& scenario) const noexcept
{
    ReferenceSolveResult result {};
    result.converged = scenario.isValid();
    result.iterations = result.converged ? 1 : 0;
    result.outputLevelDbu = scenario.inputLevelDbu;
    return result;
}

} // namespace u273::reference
