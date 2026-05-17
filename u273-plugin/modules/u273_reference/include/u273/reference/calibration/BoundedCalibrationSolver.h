#pragma once

#include <string>
#include <vector>

#include "u273/reference/calibration/ActiveModelParameters.h"
#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/state_space/CircuitGraph.h"
#include "u273/reference/state_space/StateSpaceSolver.h"

namespace u273::reference::calibration {

struct BoundedCalibrationOptions {
    bool includeDc {true};
    bool includeAc {true};
    bool includeTransient {true};
    int maxIterations {8};
    int coarseGridLevels {3};
    double finiteDifferenceRelative {1.0e-4};
    double initialDamping {1.0e-2};
    double maxNormalizedStep {0.25};
    double convergenceTolerance {1.0e-9};
    u273::reference::state_space::SolverOptions transient {
        192000.0,
        u273::reference::state_space::IntegrationMethod::trBdf2,
        {}};
};

struct BoundedCalibrationProblem {
    ActiveModelParameters initialParameters {};
    CalibrationDataset dataset {};
    u273::reference::state_space::CircuitGraph referenceCircuit {};
    std::vector<std::string> trainingDcScenarios {};
    std::vector<std::string> validationDcScenarios {};
};

struct CalibrationResidualVector {
    bool validInput {};
    bool dcPassed {};
    bool acPassed {};
    bool transientPassed {};
    std::vector<double> normalizedResiduals {};
    std::vector<std::string> failures {};

    [[nodiscard]] double cost() const noexcept;
};

struct BoundedCalibrationResult {
    bool validInput {};
    bool converged {};
    bool validationPassed {};
    int iterations {};
    ActiveModelParameters bestParameters {};
    double initialTrainCost {};
    double trainCost {};
    double validationCost {};
    std::vector<std::string> residualFailures {};
    std::vector<std::string> calibrationFailures {};
    std::vector<std::string> parametersOnBound {};
};

class BoundedCalibrationSolver {
public:
    [[nodiscard]] BoundedCalibrationResult solve(
        const BoundedCalibrationProblem& problem,
        const BoundedCalibrationOptions& options = {}) const;

    [[nodiscard]] CalibrationResidualVector evaluate(
        const BoundedCalibrationProblem& problem,
        const ActiveModelParameters& parameters,
        const std::vector<std::string>& dcScenarios,
        const BoundedCalibrationOptions& options = {}) const;
};

[[nodiscard]] std::vector<std::string> activeModelParameterNames();
[[nodiscard]] double activeModelParameterValue(const ActiveModelParameters& parameters, std::size_t index);
void setActiveModelParameterValue(ActiveModelParameters& parameters, std::size_t index, double value);
[[nodiscard]] const BoundedParameter& activeModelParameterAt(const ActiveModelParameters& parameters,
                                                            std::size_t index);

} // namespace u273::reference::calibration
