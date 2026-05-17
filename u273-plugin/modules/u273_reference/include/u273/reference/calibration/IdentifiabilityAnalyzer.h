#pragma once

#include <string>
#include <vector>

#include "u273/reference/calibration/BoundedCalibrationSolver.h"
#include "u273/reference/state_space/Matrix.h"

namespace u273::reference::calibration {

struct IdentifiabilityOptions {
    double perturbationRelative {1.0e-4};
    double conditionNumberMax {1.0e8};
    double minimumSensitivity {1.0e-6};
    double strongCorrelationThreshold {0.999};
};

struct ParameterCorrelation {
    std::string first {};
    std::string second {};
    double correlation {};
};

struct IdentifiabilityResult {
    bool validInput {};
    bool passed {};
    std::vector<double> singularValues {};
    double conditionNumber {};
    std::vector<std::string> weakParameters {};
    std::vector<std::string> parametersOnBound {};
    std::vector<ParameterCorrelation> strongCorrelations {};
    std::vector<std::string> failures {};
};

class IdentifiabilityAnalyzer {
public:
    [[nodiscard]] IdentifiabilityResult analyze(
        const BoundedCalibrationProblem& problem,
        const ActiveModelParameters& parameters,
        const BoundedCalibrationOptions& calibrationOptions = {},
        const IdentifiabilityOptions& options = {}) const;

    [[nodiscard]] IdentifiabilityResult analyzeSensitivityMatrix(
        const std::vector<std::string>& parameterNames,
        const u273::reference::state_space::DenseMatrix& sensitivity,
        const std::vector<std::string>& parametersOnBound = {},
        const IdentifiabilityOptions& options = {}) const;
};

} // namespace u273::reference::calibration
