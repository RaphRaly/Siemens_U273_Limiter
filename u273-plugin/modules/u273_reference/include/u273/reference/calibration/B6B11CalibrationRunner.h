#pragma once

#include <filesystem>

#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/CalibrationReport.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/state_space/StateSpaceSolver.h"

namespace u273::reference::calibration {

struct OfflineCalibrationRunOptions {
    OperatingPointOptions operatingPoint {};
    u273::reference::state_space::SolverOptions transient {};
    int transientSteps {16};
};

class B6B11CalibrationRunner {
public:
    [[nodiscard]] CalibrationReport createInitialReport(const CalibrationDataset& dataset,
                                                        const OperatingPointResult& operatingPoint) const;

    [[nodiscard]] CalibrationReport runOffline(
        const std::filesystem::path& resultsDirectory,
        const OfflineCalibrationRunOptions& options = {}) const;
};

} // namespace u273::reference::calibration
