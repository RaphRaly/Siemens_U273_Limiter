#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "u273/reference/calibration/ActiveModelParameters.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

struct ReductionBuildOptions {
    double sampleRate {48000.0};
    double minCommandVolt {0.0};
    double maxCommandVolt {3.0};
    int commandPoints {65};
    double monotonicToleranceDb {1.0e-6};
    double derivativeLimitDbPerVolt {240.0};
};

struct ReductionTablePoint {
    double commandVolt {};
    double gainReductionDb {};
    double dGainReductionDbDCommand {};
};

struct ReductionBuildResult {
    bool validInput {};
    bool built {};
    bool monotonic {};
    bool smoothC0 {};
    bool smoothC1 {};
    std::vector<ReductionTablePoint> points {};
    std::vector<std::string> failures {};
};

class ReductionBuilder {
public:
    [[nodiscard]] ReductionBuildResult build(
        const ActiveModelParameters& parameters,
        const u273::reference::state_space::CircuitGraph& referenceCircuit,
        const ReductionBuildOptions& options = {}) const;
};

[[nodiscard]] bool writeReductionTableCsv(const ReductionBuildResult& result,
                                          const std::filesystem::path& path);
[[nodiscard]] bool writeReductionTableJson(const ReductionBuildResult& result,
                                           const std::filesystem::path& path);

} // namespace u273::reference::calibration
