#pragma once

#include <string>
#include <vector>

#include "u273/core/ModelBoundary.h"
#include "u273/reference/calibration/ActiveModelParameters.h"

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

struct CalibrationReport {
    u273::core::ModelBoundary boundary {u273::core::ModelBoundary::fullActiveModelUnverified};
    ActiveModelParameters parameters {};
    CalibrationGateStatus gates {};
    std::vector<std::string> notes {};
    std::vector<std::string> rejectedTopologyReasons {};
    std::vector<std::string> nonIdentifiableParameters {};

    [[nodiscard]] bool canPromoteBoundary() const noexcept
    {
        return boundary == u273::core::ModelBoundary::fullActiveModelUnverified
            && parameters.isValid()
            && !parameters.hasParameterOnBound()
            && gates.allPassed()
            && rejectedTopologyReasons.empty()
            && nonIdentifiableParameters.empty();
    }
};

} // namespace u273::reference::calibration
