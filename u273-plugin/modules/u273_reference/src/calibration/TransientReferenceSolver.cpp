#include "u273/reference/calibration/TransientReferenceSolver.h"

#include <algorithm>
#include <array>
#include <string>

namespace u273::reference::calibration {

TransientReferenceResult TransientReferenceSolver::run(
    const u273::reference::state_space::CircuitGraph& circuit,
    u273::reference::state_space::CircuitState initialState,
    const u273::reference::state_space::SolverOptions& options,
    int steps) const
{
    TransientReferenceResult result {};
    result.validInput = options.isValid() && initialState.isValidFor(circuit) && steps > 0;
    if (!result.validInput) {
        result.failures.push_back("invalid transient reference input");
        return result;
    }

    u273::reference::state_space::StateSpaceSolver solver {circuit};
    result.diagnostics.reserve(static_cast<std::size_t>(steps));
    auto state = std::move(initialState);
    constexpr std::array<int, 3> retrySubstepFactors {2, 4, 8};

    for (auto index = 0; index < steps; ++index) {
        const auto previousState = state;
        const auto step = solver.step(state, options);
        result.diagnostics.push_back(step);
        result.maxResidualNorm = std::max(result.maxResidualNorm, step.residualNorm);
        ++result.totalSolverSteps;
        if (step.validInput && step.converged) {
            result.steps = index + 1;
            continue;
        }

        if (result.firstFailedStep < 0) {
            result.firstFailedStep = index;
        }

        auto recovered = false;
        for (const auto factor : retrySubstepFactors) {
            auto retryState = previousState;
            auto retryOptions = options;
            retryOptions.sampleRate = options.sampleRate * static_cast<double>(factor);
            auto allSubstepsConverged = retryOptions.isValid();

            for (auto substep = 0; allSubstepsConverged && substep < factor; ++substep) {
                const auto retryStep = solver.step(retryState, retryOptions);
                result.diagnostics.push_back(retryStep);
                result.maxResidualNorm = std::max(result.maxResidualNorm, retryStep.residualNorm);
                ++result.totalSolverSteps;
                allSubstepsConverged = retryStep.validInput && retryStep.converged;
            }

            if (allSubstepsConverged) {
                state = std::move(retryState);
                ++result.retryCount;
                result.steps = index + 1;
                recovered = true;
                break;
            }
        }

        if (!recovered) {
            result.converged = false;
            result.failures.push_back("transient step " + std::to_string(index)
                                      + " did not converge, including offline substep retries");
            return result;
        }
    }

    result.converged = true;
    return result;
}

} // namespace u273::reference::calibration
