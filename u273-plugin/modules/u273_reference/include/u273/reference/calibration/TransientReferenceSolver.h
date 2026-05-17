#pragma once

#include <string>
#include <vector>

#include "u273/reference/state_space/StateSpaceSolver.h"

namespace u273::reference::calibration {

struct TransientReferenceResult {
    bool validInput {};
    bool converged {};
    int steps {};
    int totalSolverSteps {};
    int retryCount {};
    int firstFailedStep {-1};
    double maxResidualNorm {};
    double maxDeltaNorm {};
    double convergenceRatio {};
    std::vector<u273::reference::state_space::StepResult> diagnostics {};
    std::vector<std::string> failures {};
};

class TransientReferenceSolver {
public:
    [[nodiscard]] TransientReferenceResult run(
        const u273::reference::state_space::CircuitGraph& circuit,
        u273::reference::state_space::CircuitState initialState,
        const u273::reference::state_space::SolverOptions& options,
        int steps) const;
};

} // namespace u273::reference::calibration
