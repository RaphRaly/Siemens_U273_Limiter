#pragma once

#include <string>
#include <vector>

#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

struct AcLinearizationPoint {
    double frequencyHz {};
    double magnitudeLinear {};
    double magnitudeDb {};
    double phaseRadians {};
};

struct AcSourcePort {
    std::string voltageSourceId {};
    u273::reference::state_space::NodeId outputNode {};
    double amplitudeVolt {1.0};
};

struct AcLinearizationResult {
    bool validInput {};
    bool solved {};
    std::vector<AcLinearizationPoint> points {};
    std::vector<std::string> failures {};
};

class LinearizedAcSolver {
public:
    [[nodiscard]] AcLinearizationResult solveSmallSignal(
        const u273::reference::state_space::CircuitGraph& circuit,
        const OperatingPointResult& operatingPoint,
        const AcSourcePort& source,
        const std::vector<double>& frequenciesHz) const;
};

} // namespace u273::reference::calibration
