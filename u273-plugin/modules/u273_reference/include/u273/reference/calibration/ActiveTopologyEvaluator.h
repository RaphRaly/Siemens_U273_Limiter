#pragma once

#include <string>
#include <vector>

#include "u273/reference/calibration/ActiveTopologyCandidate.h"
#include "u273/reference/calibration/OperatingPointSolver.h"

namespace u273::reference::calibration {

struct ActiveTopologyEvaluationOptions {
    double minForwardVbeVolt {0.45};
    double maxForwardVbeVolt {0.85};
    double minForwardVceVolt {0.2};
    double localKclToleranceAmp {1.0e-4};
    bool requireAcImprovement {true};
};

struct ActiveTopologyEvaluationResult {
    ActiveTopologyCandidate candidate {};
    bool evaluated {};
    bool dcPlausible {};
    bool localKclPlausible {};
    bool acImproves {};
    bool accepted {};
    double vbeVolt {};
    double vceVolt {};
    double localKclAmp {};
    std::string reason {};
};

class ActiveTopologyEvaluator {
public:
    [[nodiscard]] ActiveTopologyEvaluationResult evaluate(
        const u273::reference::state_space::CircuitGraph& circuit,
        const OperatingPointResult& operatingPoint,
        const ActiveTopologyCandidate& candidate,
        const ActiveTopologyEvaluationOptions& options = {}) const;

    [[nodiscard]] std::vector<ActiveTopologyEvaluationResult> evaluateB6GuardedCandidates(
        const u273::reference::state_space::CircuitGraph& circuit,
        const OperatingPointResult& operatingPoint,
        const ActiveTopologyEvaluationOptions& options = {}) const;
};

} // namespace u273::reference::calibration
