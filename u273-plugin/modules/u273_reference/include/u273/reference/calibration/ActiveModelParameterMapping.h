#pragma once

#include "u273/reference/calibration/ActiveModelParameters.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

[[nodiscard]] u273::reference::state_space::CircuitGraph applyActiveModelParametersToCircuit(
    u273::reference::state_space::CircuitGraph circuit,
    const ActiveModelParameters& parameters);

} // namespace u273::reference::calibration
