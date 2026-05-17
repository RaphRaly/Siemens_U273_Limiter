#include "u273/reference/calibration/SonicTarget.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace u273::reference::calibration {

namespace {

constexpr int kExpectedColumnCount = 13;

[[nodiscard]] std::string trim(std::string_view input)
{
    const auto begin = input.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return std::string {input.substr(begin, end - begin + 1)};
}

[[nodiscard]] std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> fields {};
    std::string current {};
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const auto c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    fields.push_back(trim(current));
    return fields;
}

[[nodiscard]] double parseOptionalDouble(const std::string& text, bool& ok) noexcept
{
    ok = true;
    if (text.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    errno = 0;
    char* end = nullptr;
    const auto value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || errno == ERANGE) {
        ok = false;
        return std::numeric_limits<double>::quiet_NaN();
    }
    while (end != nullptr && *end != '\0') {
        if (*end != ' ' && *end != '\t') {
            ok = false;
            return std::numeric_limits<double>::quiet_NaN();
        }
        ++end;
    }
    return value;
}

[[nodiscard]] SonicThdTargetRow parseRow(const std::vector<std::string>& fields,
                                         int lineNumber,
                                         std::vector<std::string>& failures)
{
    SonicThdTargetRow row {};
    row.targetId = fields[0];
    row.measurementNode = fields[1];
    row.mode = fields[2];
    row.sourceType = fields[10];
    row.confidence = fields[11];
    row.notes = fields[12];

    auto parse = [&](int index, const char* name) {
        bool ok = true;
        const auto value = parseOptionalDouble(fields[static_cast<std::size_t>(index)], ok);
        if (!ok) {
            std::ostringstream s;
            s << "line " << lineNumber << ": invalid numeric field " << name;
            failures.push_back(s.str());
        }
        return value;
    };

    row.frequencyHz = parse(3, "frequency_hz");
    row.outputLevelDbu = parse(4, "output_level_dbu");
    row.gainReductionDb = parse(5, "gain_reduction_db");
    row.thdMinPercent = parse(6, "thd_min_percent");
    row.thdTargetPercent = parse(7, "thd_target_percent");
    row.thdMaxPercent = parse(8, "thd_max_percent");
    row.thdTargetDb = parse(9, "thd_target_db");

    if (row.targetId.empty()) {
        failures.push_back("line " + std::to_string(lineNumber) + ": missing target_id");
    }
    if (row.measurementNode.empty()) {
        failures.push_back("line " + std::to_string(lineNumber) + ": missing measurement_node");
    }
    if (row.mode.empty()) {
        failures.push_back("line " + std::to_string(lineNumber) + ": missing mode");
    }
    if (!row.hasFiniteTargetDb() && !row.hasFiniteCeilingPercent()) {
        failures.push_back("line " + std::to_string(lineNumber) + ": missing THD target or ceiling");
    }

    return row;
}

[[nodiscard]] bool headerLooksExpected(const std::vector<std::string>& header)
{
    static const std::vector<std::string> expected {
        "target_id",
        "measurement_node",
        "mode",
        "frequency_hz",
        "output_level_dbu",
        "gain_reduction_db",
        "thd_min_percent",
        "thd_target_percent",
        "thd_max_percent",
        "thd_target_db",
        "source_type",
        "confidence",
        "notes"};
    return header == expected;
}

} // namespace

bool SonicThdTargetRow::hasFiniteTargetDb() const noexcept
{
    return std::isfinite(thdTargetDb);
}

bool SonicThdTargetRow::hasFiniteCeilingPercent() const noexcept
{
    return std::isfinite(thdMaxPercent) && thdMaxPercent > 0.0;
}

std::filesystem::path defaultSonicThdTargetCsvPath()
{
#ifdef U273_ASSET_ROOT
    return std::filesystem::path {U273_ASSET_ROOT}
        / "calibration"
        / "u273_thd_texture_target_v1.csv";
#else
    return std::filesystem::path {"modules"}
        / "u273_assets"
        / "calibration"
        / "u273_thd_texture_target_v1.csv";
#endif
}

double thdPercentToDb(double percent) noexcept
{
    if (!std::isfinite(percent) || percent <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return 20.0 * std::log10(percent / 100.0);
}

SonicThdTargetLoadResult loadSonicThdTargetCsv(const std::filesystem::path& path)
{
    SonicThdTargetLoadResult result {};
    result.path = path;

    std::ifstream in {path};
    if (!in) {
        result.failures.push_back("could not open THD sonic target CSV: " + path.string());
        return result;
    }

    std::string line {};
    int lineNumber = 0;
    bool sawHeader = false;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (line.empty()) {
            continue;
        }

        const auto fields = splitCsvLine(line);
        if (!sawHeader) {
            sawHeader = true;
            if (!headerLooksExpected(fields)) {
                result.failures.push_back("unexpected THD sonic target CSV header");
                return result;
            }
            continue;
        }

        if (static_cast<int>(fields.size()) != kExpectedColumnCount) {
            std::ostringstream s;
            s << "line " << lineNumber << ": expected " << kExpectedColumnCount
              << " columns, got " << fields.size();
            result.failures.push_back(s.str());
            continue;
        }

        result.rows.push_back(parseRow(fields, lineNumber, result.failures));
    }

    if (!sawHeader) {
        result.failures.push_back("empty THD sonic target CSV");
    }
    if (result.rows.empty()) {
        result.failures.push_back("THD sonic target CSV contains no target rows");
    }
    result.loaded = result.failures.empty();
    return result;
}

const SonicThdTargetRow* findThdTargetByNodeAndMode(const std::vector<SonicThdTargetRow>& rows,
                                                    const std::string& measurementNode,
                                                    const std::string& mode) noexcept
{
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const SonicThdTargetRow& row) {
        return row.measurementNode == measurementNode && row.mode == mode;
    });
    return found == rows.end() ? nullptr : &(*found);
}

} // namespace u273::reference::calibration
