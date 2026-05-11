#pragma once

#include <string>

#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

enum class ActiveDeviceKind {
    unknown = 0,
    npn,
    pnp
};

struct ActiveTopologyCandidate {
    std::string id {};
    std::string device {};
    ActiveDeviceKind kind {ActiveDeviceKind::unknown};
    u273::reference::state_space::NodeId collector {-1};
    u273::reference::state_space::NodeId base {-1};
    u273::reference::state_space::NodeId emitter {-1};
    double priorScore {};
    std::string rejectionReason {};

    [[nodiscard]] bool hasUsablePins() const noexcept;
    [[nodiscard]] bool isRejected() const noexcept { return !rejectionReason.empty(); }
    void reject(std::string reason);
};

} // namespace u273::reference::calibration
