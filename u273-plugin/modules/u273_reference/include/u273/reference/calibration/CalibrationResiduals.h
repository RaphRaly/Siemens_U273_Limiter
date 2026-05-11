#pragma once

#include <string>
#include <vector>

#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

enum class ResidualStatus {
    passed = 0,
    failed,
    skipped
};

struct ResidualPoint {
    std::string name {};
    std::string context {};
    std::string unit {};
    double measured {};
    double model {};
    double rawError {};
    double tolerance {};
    double weightedError {};
    ResidualStatus status {ResidualStatus::failed};

    [[nodiscard]] bool passed() const noexcept { return status == ResidualStatus::passed; }
    [[nodiscard]] bool failed() const noexcept { return status == ResidualStatus::failed; }
};

struct ResidualSet {
    std::string name {};
    bool validInput {};
    bool evaluated {};
    std::vector<ResidualPoint> points {};
    std::vector<std::string> notes {};
    std::vector<std::string> failures {};

    [[nodiscard]] bool passed() const noexcept;
};

struct ResidualGateResult {
    std::string gateName {};
    bool validInput {};
    bool evaluated {};
    bool passed {};
    double worstWeightedError {};
    std::vector<ResidualSet> sets {};
    std::vector<std::string> notes {};
    std::vector<std::string> failures {};
};

struct DcResidualOptions {
    double voltageToleranceVolt {0.025};
    double sourceVoltageToleranceVolt {0.002};
};

struct AcResidualOptions {
    std::string target {"VCMD"};
    std::string scenario {};
    double driveVolt {};
    bool matchDriveVolt {};
    double commandSourceOhm {};
    bool matchCommandSourceOhm {};
    double magnitudeToleranceDb {0.5};
    double phaseToleranceRadians {0.08726646259971647};
};

struct TransientModelSummary {
    std::string caseName {};
    bool hasMaxDriveVolt {};
    double maxDriveVolt {};
    bool hasMaxCmdVolt {};
    double maxCmdVolt {};
    bool hasAttack90TimeSeconds {};
    double attack90TimeSeconds {};
    bool hasRelease10TimeSeconds {};
    double release10TimeSeconds {};
};

struct TransientResidualOptions {
    double voltageToleranceVolt {0.005};
    double timeToleranceSeconds {0.05};
    bool requireRelease10TimeSeconds {};
};

[[nodiscard]] ResidualGateResult evaluateDcResiduals(
    const CalibrationDataset& dataset,
    const u273::reference::state_space::CircuitGraph& circuit,
    const OperatingPointResult& operatingPoint,
    const DcResidualOptions& options = {});

[[nodiscard]] ResidualGateResult evaluateAcResiduals(
    const CalibrationDataset& dataset,
    const AcLinearizationResult& ac,
    const AcResidualOptions& options = {});

[[nodiscard]] ResidualGateResult evaluateAcResiduals(
    const std::vector<AcGoldenPoint>& goldenPoints,
    const AcLinearizationResult& ac,
    const AcResidualOptions& options = {});

[[nodiscard]] ResidualGateResult evaluateTransientResiduals(
    const CalibrationDataset& dataset,
    const std::vector<TransientModelSummary>& modelSummaries,
    const TransientResidualOptions& options = {});

[[nodiscard]] ResidualGateResult evaluateTransientResiduals(
    const std::vector<TransientGoldenSummary>& goldenSummaries,
    const std::vector<TransientModelSummary>& modelSummaries,
    const TransientResidualOptions& options = {});

} // namespace u273::reference::calibration
