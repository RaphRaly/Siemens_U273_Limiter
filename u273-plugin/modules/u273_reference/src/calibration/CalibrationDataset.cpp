#include "u273/reference/calibration/CalibrationDataset.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace u273::reference::calibration {

namespace {

[[nodiscard]] std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input {path, std::ios::binary};
    if (!input) {
        return {};
    }

    std::ostringstream stream {};
    stream << input.rdbuf();
    return stream.str();
}

[[nodiscard]] std::size_t findJsonKey(std::string_view text, std::string_view key, std::size_t offset = 0)
{
    std::string needle {};
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key);
    needle.push_back('"');
    return text.find(needle, offset);
}

[[nodiscard]] std::optional<std::string> extractString(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
        ++colon;
    }
    if (colon >= text.size() || text[colon] != '"') {
        return std::nullopt;
    }

    ++colon;
    std::string value {};
    auto escaped = false;
    for (auto index = colon; index < text.size(); ++index) {
        const auto ch = text[index];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> extractNumber(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
        ++colon;
    }

    const auto* begin = text.data() + colon;
    char* end {};
    const auto value = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<bool> extractBool(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
        ++colon;
    }
    if (text.substr(colon, 4) == "true") {
        return true;
    }
    if (text.substr(colon, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> extractObject(std::string_view text, std::string_view key)
{
    const auto keyPos = findJsonKey(text, key);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto objectBegin = text.find('{', keyPos);
    if (objectBegin == std::string_view::npos) {
        return std::nullopt;
    }

    auto inString = false;
    auto escaped = false;
    auto depth = 0;
    for (auto index = objectBegin; index < text.size(); ++index) {
        const auto ch = text[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(objectBegin, index - objectBegin + 1);
            }
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::size_t findArrayBegin(std::string_view text, std::string_view arrayKey)
{
    const auto keyPos = findJsonKey(text, arrayKey);
    if (keyPos == std::string_view::npos) {
        return std::string_view::npos;
    }
    return text.find('[', keyPos);
}

[[nodiscard]] std::vector<std::string_view> extractObjectArray(std::string_view text, std::string_view arrayKey)
{
    std::vector<std::string_view> objects {};
    const auto arrayBegin = findArrayBegin(text, arrayKey);
    if (arrayBegin == std::string_view::npos) {
        return objects;
    }

    auto inString = false;
    auto escaped = false;
    auto depth = 0;
    std::size_t objectBegin = std::string_view::npos;

    for (auto index = arrayBegin + 1; index < text.size(); ++index) {
        const auto ch = text[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                objectBegin = index;
            }
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && objectBegin != std::string_view::npos) {
                objects.push_back(text.substr(objectBegin, index - objectBegin + 1));
                objectBegin = std::string_view::npos;
            }
            continue;
        }
        if (ch == ']' && depth == 0) {
            break;
        }
    }

    return objects;
}

[[nodiscard]] int countNumberArrayEntries(std::string_view text, std::string_view arrayKey)
{
    const auto arrayBegin = findArrayBegin(text, arrayKey);
    if (arrayBegin == std::string_view::npos) {
        return 0;
    }
    const auto arrayEnd = text.find(']', arrayBegin);
    if (arrayEnd == std::string_view::npos) {
        return 0;
    }

    auto count = 0;
    auto cursor = arrayBegin + 1;
    while (cursor < arrayEnd) {
        while (cursor < text.size()
               && (std::isspace(static_cast<unsigned char>(text[cursor])) || text[cursor] == ',')) {
            ++cursor;
        }
        if (cursor >= arrayEnd) {
            break;
        }
        const auto* begin = text.data() + cursor;
        char* end {};
        const auto value = std::strtod(begin, &end);
        if (end != begin && std::isfinite(value)) {
            ++count;
            cursor = static_cast<std::size_t>(end - text.data());
            continue;
        }
        ++cursor;
    }
    return count;
}

[[nodiscard]] std::vector<double> extractNumberArray(std::string_view text, std::string_view arrayKey)
{
    std::vector<double> values {};
    const auto arrayBegin = findArrayBegin(text, arrayKey);
    if (arrayBegin == std::string_view::npos) {
        return values;
    }
    const auto arrayEnd = text.find(']', arrayBegin);
    if (arrayEnd == std::string_view::npos) {
        return values;
    }

    auto cursor = arrayBegin + 1;
    while (cursor < arrayEnd) {
        while (cursor < text.size()
               && (std::isspace(static_cast<unsigned char>(text[cursor])) || text[cursor] == ',')) {
            ++cursor;
        }
        if (cursor >= arrayEnd) {
            break;
        }
        const auto* begin = text.data() + cursor;
        char* end {};
        const auto value = std::strtod(begin, &end);
        if (end != begin && std::isfinite(value)) {
            values.push_back(value);
            cursor = static_cast<std::size_t>(end - text.data());
            continue;
        }
        ++cursor;
    }

    return values;
}

void addFailureIfMissing(CalibrationDataset& dataset,
                         std::string_view text,
                         const char* fileName)
{
    if (text.empty()) {
        dataset.failures.push_back(std::string {"missing or empty golden file: "} + fileName);
    }
}

void buildDeterministicSplit(CalibrationDataset& dataset)
{
    const auto midpoint = dataset.dcScenarios.size() / 2;
    for (std::size_t index = 0; index < dataset.dcScenarios.size(); ++index) {
        auto& target = index < midpoint
            ? dataset.split.trainingDcScenarios
            : dataset.split.validationDcScenarios;
        target.push_back(dataset.dcScenarios[index].name);
    }

    for (const auto& transientCase : dataset.transientCases) {
        dataset.split.acceptanceTransientCases.push_back(transientCase.name);
    }
}

} // namespace

bool CalibrationDataset::isValid() const noexcept
{
    return failures.empty()
        && !dcScenarios.empty()
        && !transientCases.empty()
        && !transientSummaries.empty()
        && !transientRowsData.empty()
        && !acPoints.empty()
        && acRows > 0
        && acFrequencyCount > 0
        && transientRows > 0
        && !split.trainingDcScenarios.empty()
        && !split.validationDcScenarios.empty()
        && !split.acceptanceTransientCases.empty();
}

const DcGoldenScenario* CalibrationDataset::findDcScenario(const std::string& name) const noexcept
{
    for (const auto& scenario : dcScenarios) {
        if (scenario.name == name) {
            return &scenario;
        }
    }
    return nullptr;
}

const TransientGoldenSummary* CalibrationDataset::findTransientSummary(const std::string& caseName) const noexcept
{
    for (const auto& summary : transientSummaries) {
        if (summary.caseName == caseName) {
            return &summary;
        }
    }
    return nullptr;
}

std::vector<TransientGoldenRow> CalibrationDataset::transientRowsForCase(const std::string& caseName) const
{
    std::vector<TransientGoldenRow> rows {};
    for (const auto& row : transientRowsData) {
        if (row.caseName == caseName) {
            rows.push_back(row);
        }
    }
    return rows;
}

CalibrationDataset CalibrationDataset::loadFromResultsDirectory(const std::filesystem::path& resultsDirectory)
{
    CalibrationDataset dataset {};
    dataset.resultsDirectory = resultsDirectory;

    const auto reportText = readTextFile(resultsDirectory / "u273_validation_report.json");
    const auto dcText = readTextFile(resultsDirectory / "u273_dc_global.json");
    const auto acText = readTextFile(resultsDirectory / "u273_ac_global.json");
    const auto transientText = readTextFile(resultsDirectory / "u273_transient_attack_release.json");

    addFailureIfMissing(dataset, reportText, "u273_validation_report.json");
    addFailureIfMissing(dataset, dcText, "u273_dc_global.json");
    addFailureIfMissing(dataset, acText, "u273_ac_global.json");
    addFailureIfMissing(dataset, transientText, "u273_transient_attack_release.json");
    if (!dataset.failures.empty()) {
        return dataset;
    }

    dataset.dcStatus = extractString(dcText, "status").value_or(std::string {});
    dataset.acStatus = extractString(acText, "status").value_or(std::string {});
    dataset.transientStatus = extractString(transientText, "status").value_or(std::string {});
    dataset.acRows = static_cast<int>(extractNumber(reportText, "acRows").value_or(0.0));
    dataset.transientRows = static_cast<int>(extractNumber(reportText, "transientRows").value_or(0.0));
    dataset.acFrequenciesHz = extractNumberArray(acText, "frequencies");
    dataset.acFrequencyCount = static_cast<int>(dataset.acFrequenciesHz.size());
    if (dataset.acFrequencyCount == 0) {
        dataset.acFrequencyCount = countNumberArrayEntries(acText, "frequencies");
    }

    for (const auto object : extractObjectArray(dcText, "results")) {
        DcGoldenScenario scenario {};
        scenario.name = extractString(object, "name").value_or(std::string {});
        scenario.commandSourceVolt = extractNumber(object, "commandSourceVolt").value_or(0.0);
        scenario.commandSourceOhm = extractNumber(object, "commandSourceOhm").value_or(0.0);
        const auto b11 = extractNumber(object, "B11_DRV");
        const auto cmd = extractNumber(object, "CMD");
        const auto nb = extractNumber(object, "NB");
        const auto nl = extractNumber(object, "NL");
        const auto nr = extractNumber(object, "NR");
        scenario.hasB11DriveVolt = b11.has_value();
        scenario.b11DriveVolt = b11.value_or(0.0);
        scenario.cmdVolt = cmd.value_or(0.0);
        scenario.nbVolt = nb.value_or(0.0);
        scenario.hasNlVolt = nl.has_value();
        scenario.nlVolt = nl.value_or(0.0);
        scenario.hasNrVolt = nr.has_value();
        scenario.nrVolt = nr.value_or(0.0);
        scenario.maxResidual = extractNumber(object, "maxAbs").value_or(0.0);
        scenario.converged = extractBool(object, "converged").value_or(false);
        if (!scenario.name.empty()) {
            dataset.dcScenarios.push_back(std::move(scenario));
        }
    }

    constexpr auto degreesToRadians = 3.1415926535897932384626433832795 / 180.0;
    for (const auto object : extractObjectArray(acText, "rows")) {
        AcGoldenPoint point {};
        point.scenario = extractString(object, "scenario").value_or(std::string {});
        point.driveVolt = extractNumber(object, "driveVolt").value_or(0.0);
        point.commandSourceOhm = extractNumber(object, "commandSourceOhm").value_or(0.0);
        point.cmdDcVolt = extractNumber(object, "cmdDcVolt").value_or(0.0);
        point.frequencyHz = extractNumber(object, "frequency").value_or(0.0);

        if (const auto vcmd = extractObject(object, "VCMD")) {
            const auto db = extractNumber(*vcmd, "db");
            const auto phaseDeg = extractNumber(*vcmd, "phaseDeg");
            point.hasCmd = db.has_value() && phaseDeg.has_value();
            point.cmdMagnitudeDb = db.value_or(0.0);
            point.cmdPhaseRadians = phaseDeg.value_or(0.0) * degreesToRadians;
        }
        if (const auto vnb = extractObject(object, "VNB")) {
            const auto db = extractNumber(*vnb, "db");
            const auto phaseDeg = extractNumber(*vnb, "phaseDeg");
            point.hasNb = db.has_value() && phaseDeg.has_value();
            point.nbMagnitudeDb = db.value_or(0.0);
            point.nbPhaseRadians = phaseDeg.value_or(0.0) * degreesToRadians;
        }

        if (!point.scenario.empty() && point.frequencyHz > 0.0 && (point.hasCmd || point.hasNb)) {
            dataset.acPoints.push_back(std::move(point));
        }
    }

    for (const auto object : extractObjectArray(transientText, "cases")) {
        TransientGoldenCase transientCase {};
        transientCase.name = extractString(object, "name").value_or(std::string {});
        transientCase.detectorScenario = extractString(object, "detectorScenario").value_or(std::string {});
        transientCase.ueHighRms = extractNumber(object, "ueHighRms").value_or(0.0);
        transientCase.commandSourceOhm = extractNumber(object, "commandSourceOhm").value_or(0.0);
        transientCase.detectorToDriveScale = extractNumber(object, "detectorToDriveScale").value_or(0.0);
        if (!transientCase.name.empty()) {
            dataset.transientCases.push_back(std::move(transientCase));
        }
    }

    for (const auto object : extractObjectArray(transientText, "summaries")) {
        TransientGoldenSummary summary {};
        summary.caseName = extractString(object, "case").value_or(std::string {});
        summary.maxDriveVolt = extractNumber(object, "maxDriveVolt").value_or(0.0);
        summary.maxCmdVolt = extractNumber(object, "maxCmdVolt").value_or(0.0);
        summary.attack90TimeSeconds = extractNumber(object, "attack90TimeSeconds").value_or(0.0);
        const auto release = extractNumber(object, "release10TimeSeconds");
        summary.hasRelease10Time = release.has_value();
        summary.release10TimeSeconds = release.value_or(0.0);
        if (!summary.caseName.empty()) {
            dataset.transientSummaries.push_back(std::move(summary));
        }
    }

    for (const auto object : extractObjectArray(transientText, "rows")) {
        TransientGoldenRow row {};
        row.caseName = extractString(object, "case").value_or(std::string {});
        row.timeSeconds = extractNumber(object, "time").value_or(0.0);
        row.driveVolt = extractNumber(object, "driveVolt").value_or(0.0);
        row.cmdVolt = extractNumber(object, "cmdVolt").value_or(0.0);
        row.sourceCurrentAmp = extractNumber(object, "sourceCurrentAmp").value_or(0.0);
        row.maxResidual = extractNumber(object, "maxResidual").value_or(0.0);
        if (!row.caseName.empty()) {
            dataset.transientRowsData.push_back(std::move(row));
        }
    }

    if (dataset.dcStatus != "PARAMETRIC_THEVENIN_REFERENCE") {
        dataset.failures.push_back("unexpected DC golden status");
    }
    if (dataset.acStatus != "PARAMETRIC_THEVENIN_B6_BRIDGE_REFERENCE") {
        dataset.failures.push_back("unexpected AC golden status");
    }
    if (dataset.transientStatus != "BOUNDED_QUASI_STATIC_TRANSIENT") {
        dataset.failures.push_back("unexpected transient golden status");
    }

    buildDeterministicSplit(dataset);
    return dataset;
}

} // namespace u273::reference::calibration
