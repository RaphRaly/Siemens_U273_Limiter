#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace u273::reference::calibration {

struct DcGoldenScenario {
    std::string name {};
    double commandSourceVolt {};
    double commandSourceOhm {};
    bool hasB11DriveVolt {};
    double b11DriveVolt {};
    double cmdVolt {};
    double nbVolt {};
    bool hasNlVolt {};
    double nlVolt {};
    bool hasNrVolt {};
    double nrVolt {};
    double maxResidual {};
    bool converged {};
};

struct AcGoldenPoint {
    std::string scenario {};
    double driveVolt {};
    double commandSourceOhm {};
    double cmdDcVolt {};
    double frequencyHz {};
    bool hasCmd {};
    double cmdMagnitudeDb {};
    double cmdPhaseRadians {};
    bool hasNb {};
    double nbMagnitudeDb {};
    double nbPhaseRadians {};
};

struct TransientGoldenCase {
    std::string name {};
    std::string detectorScenario {};
    double ueHighRms {};
    double commandSourceOhm {};
    double detectorToDriveScale {};
};

struct TransientGoldenSummary {
    std::string caseName {};
    double maxDriveVolt {};
    double maxCmdVolt {};
    double attack90TimeSeconds {};
    bool hasRelease10Time {};
    double release10TimeSeconds {};
};

struct TransientGoldenRow {
    std::string caseName {};
    double timeSeconds {};
    double driveVolt {};
    double cmdVolt {};
    double sourceCurrentAmp {};
    double maxResidual {};
};

struct CalibrationDatasetSplit {
    std::vector<std::string> trainingDcScenarios {};
    std::vector<std::string> validationDcScenarios {};
    std::vector<std::string> acceptanceTransientCases {};
};

struct CalibrationDataset {
    std::filesystem::path resultsDirectory {};
    std::string dcStatus {};
    std::string acStatus {};
    std::string transientStatus {};
    int acRows {};
    int acFrequencyCount {};
    int transientRows {};
    std::vector<DcGoldenScenario> dcScenarios {};
    std::vector<double> acFrequenciesHz {};
    std::vector<AcGoldenPoint> acPoints {};
    std::vector<TransientGoldenCase> transientCases {};
    std::vector<TransientGoldenSummary> transientSummaries {};
    std::vector<TransientGoldenRow> transientRowsData {};
    CalibrationDatasetSplit split {};
    std::vector<std::string> failures {};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] const DcGoldenScenario* findDcScenario(const std::string& name) const noexcept;
    [[nodiscard]] const TransientGoldenSummary* findTransientSummary(const std::string& caseName) const noexcept;
    [[nodiscard]] std::vector<TransientGoldenRow> transientRowsForCase(const std::string& caseName) const;

    [[nodiscard]] static CalibrationDataset loadFromResultsDirectory(const std::filesystem::path& resultsDirectory);
};

} // namespace u273::reference::calibration
