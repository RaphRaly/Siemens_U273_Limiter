#pragma once

#include <vector>

#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

[[nodiscard]] AcLinearizationResult solveB6SmallSignalAcReference(
    const u273::reference::state_space::CircuitGraph& dcCircuit,
    const OperatingPointResult& dcOperatingPoint,
    double commandPortResistanceOhm,
    const std::vector<double>& frequenciesHz);

} // namespace u273::reference::calibration
