#pragma once

#include <string>
#include <vector>

#include "u273/reference/state_space/CircuitGraph.h"
#include "u273/reference/state_space/StateSpaceSolver.h"

namespace u273::reference::calibration {

struct DCCircuitView {
    u273::reference::state_space::CircuitGraph circuit {};
    int omittedCapacitors {};
    int preservedResistors {};
    int preservedDiodes {};
    int preservedBjts {};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] static DCCircuitView fromCircuit(const u273::reference::state_space::CircuitGraph& circuit);
};

struct OperatingPointOptions {
    u273::reference::state_space::SolverOptions solver {};
    int maxAttempts {4};
    bool enableDampedRetry {true};
    bool enableGminStepping {true};
    bool enableSourceStepping {true};
    int gminSteps {5};
    double initialGminSiemens {1.0e-6};
    double finalGminSiemens {1.0e-12};
    int sourceSteps {5};
    double initialSourceScale {0.1};
};

struct OperatingPointAttemptLog {
    std::string strategy {};
    int iterations {};
    double residualNorm {};
    bool converged {};
    int continuationSteps {};
};

struct OperatingPointResult {
    bool validInput {};
    bool converged {};
    int attempts {};
    u273::reference::state_space::StepResult step {};
    u273::reference::state_space::CircuitState state {};
    std::vector<OperatingPointAttemptLog> attemptLog {};
    std::vector<std::string> failures {};
};

class OperatingPointSolver {
public:
    explicit OperatingPointSolver(OperatingPointOptions options = {});

    [[nodiscard]] OperatingPointResult solve(const DCCircuitView& view) const;
    [[nodiscard]] OperatingPointResult solve(const DCCircuitView& view,
                                             const u273::reference::state_space::Vector& initialUnknowns) const;

private:
    OperatingPointOptions options_ {};
};

} // namespace u273::reference::calibration
