#pragma once

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace u273::reference::calibration {

struct SonicThdTargetRow {
    std::string targetId {};
    std::string measurementNode {};
    std::string mode {};
    double frequencyHz {std::numeric_limits<double>::quiet_NaN()};
    double outputLevelDbu {std::numeric_limits<double>::quiet_NaN()};
    double gainReductionDb {std::numeric_limits<double>::quiet_NaN()};
    double thdMinPercent {std::numeric_limits<double>::quiet_NaN()};
    double thdTargetPercent {std::numeric_limits<double>::quiet_NaN()};
    double thdMaxPercent {std::numeric_limits<double>::quiet_NaN()};
    double thdTargetDb {std::numeric_limits<double>::quiet_NaN()};
    std::string sourceType {};
    std::string confidence {};
    std::string notes {};

    [[nodiscard]] bool hasFiniteTargetDb() const noexcept;
    [[nodiscard]] bool hasFiniteCeilingPercent() const noexcept;
};

struct SonicThdTargetLoadResult {
    bool loaded {};
    std::filesystem::path path {};
    std::vector<SonicThdTargetRow> rows {};
    std::vector<std::string> failures {};
};

[[nodiscard]] std::filesystem::path defaultSonicThdTargetCsvPath();

[[nodiscard]] double thdPercentToDb(double percent) noexcept;

[[nodiscard]] SonicThdTargetLoadResult loadSonicThdTargetCsv(
    const std::filesystem::path& path = defaultSonicThdTargetCsvPath());

[[nodiscard]] const SonicThdTargetRow* findThdTargetByNodeAndMode(
    const std::vector<SonicThdTargetRow>& rows,
    const std::string& measurementNode,
    const std::string& mode) noexcept;

} // namespace u273::reference::calibration
