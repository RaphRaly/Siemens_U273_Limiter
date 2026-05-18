#pragma once

#include <string>
#include <vector>

#include "u273/core/ModelBoundary.h"
#include "u273/reference/calibration/ActiveModelParameters.h"
#include "u273/reference/calibration/ThdBench.h"

namespace u273::reference::calibration {

struct CalibrationGateStatus {
    bool referenceDc {};
    bool referenceAc {};
    bool referenceTransient {};
    bool guardedTopologyDiagnostic {};
    bool dc {};
    bool ac {};
    bool transient {};
    bool audio {};
    bool identifiability {};

    [[nodiscard]] bool allPassed() const noexcept
    {
        return dc && ac && transient && audio && identifiability;
    }
};

struct TransientRunDiagnostics {
    std::string caseName {};
    std::string method {};
    double sampleRate {};
    int steps {};
    int totalSolverSteps {};
    int retries {};
    int firstFailedStep {-1};
    double maxResidualNorm {};
    double maxDeltaNorm {};
    double convergenceRatio {};
    bool converged {};
};

struct CalibrationReport {
    u273::core::ModelBoundary boundary {u273::core::ModelBoundary::fullActiveModelUnverified};
    ActiveModelParameters parameters {};
    bool calibrationConverged {};
    bool validationPassed {};
    double calibrationTrainCost {};
    double calibrationValidationCost {};
    int calibrationIterations {};
    double identifiabilityConditionNumber {};
    std::vector<std::string> identifiabilitySensitivityParameterNames {};
    std::vector<double> identifiabilitySensitivityNorms {};
    std::vector<std::string> inactiveIdentifiabilityParameters {};
    CalibrationGateStatus gates {};
    std::vector<std::string> notes {};
    std::vector<std::string> rejectedTopologyReasons {};
    std::vector<std::string> nonIdentifiableParameters {};
    std::vector<TransientRunDiagnostics> transientDiagnostics {};
    std::vector<std::string> calibrationFailures {};
    std::vector<std::string> identifiabilityFailures {};
    std::vector<std::string> parametersOnBound {};
    std::vector<std::string> weakParameters {};
    std::vector<std::string> strongParameterCorrelations {};
    ThdBenchResult thdBench {};

    [[nodiscard]] bool canPromoteBoundary() const noexcept
    {
        return boundary == u273::core::ModelBoundary::fullActiveModelUnverified
            && parameters.isValid()
            && !parameters.hasParameterOnBound()
            && calibrationConverged
            && validationPassed
            && gates.allPassed()
            && rejectedTopologyReasons.empty()
            && nonIdentifiableParameters.empty()
            && calibrationFailures.empty()
            && identifiabilityFailures.empty()
            && parametersOnBound.empty();
    }
};

} // namespace u273::reference::calibration
