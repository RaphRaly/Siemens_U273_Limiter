#pragma once

#include <string>
#include <vector>

#include "u273/reference/calibration/ActiveTopologyCandidate.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

struct ActiveTopologyNodeSpec {
    std::string primary {};
    std::string fallback {};

    [[nodiscard]] bool hasAnyName() const noexcept;
};

struct ActiveTopologyCandidateSpec {
    std::string id {};
    std::string device {};
    ActiveDeviceKind kind {ActiveDeviceKind::unknown};
    ActiveTopologyNodeSpec collector {};
    ActiveTopologyNodeSpec base {};
    ActiveTopologyNodeSpec emitter {};
    double priorScore {};
    std::string status {};
    std::string evidence {};
    std::string blockingReason {};

    [[nodiscard]] bool hasCompleteTerminalNames() const noexcept;
};

[[nodiscard]] std::vector<ActiveTopologyCandidateSpec> defaultB6ActiveTopologyCandidateSpecs();
[[nodiscard]] std::vector<ActiveTopologyCandidateSpec> defaultB11ActiveTopologyCandidateSpecs();
[[nodiscard]] std::vector<ActiveTopologyCandidateSpec> defaultB6B11ActiveTopologyCandidateSpecs();

[[nodiscard]] std::vector<ActiveTopologyCandidate> instantiateActiveTopologyCandidates(
    const u273::reference::state_space::CircuitGraph& circuit,
    const std::vector<ActiveTopologyCandidateSpec>& specs);

} // namespace u273::reference::calibration
