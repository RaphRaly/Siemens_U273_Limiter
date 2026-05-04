#include "u273/reference/GoldenValidationSuite.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

namespace u273::reference {

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

[[nodiscard]] bool contains(std::string_view text, std::string_view token) noexcept
{
    return text.find(token) != std::string_view::npos;
}

[[nodiscard]] std::optional<double> extractNumber(std::string_view text, std::string_view key)
{
    std::string needle {};
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key);
    needle.push_back('"');

    const auto keyPos = text.find(needle);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    auto colon = text.find(':', keyPos);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    ++colon;

    const auto* begin = text.data() + colon;
    char* end {};
    const auto value = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

void requireFile(GoldenValidationSummary& summary,
                 const std::filesystem::path& path,
                 const char* label,
                 std::string& out)
{
    out = readTextFile(path);
    if (out.empty()) {
        summary.failures.push_back(std::string {"missing or empty golden file: "} + label);
    }
}

void requireContains(GoldenValidationSummary& summary,
                     std::string_view text,
                     std::string_view token,
                     const char* message)
{
    if (!contains(text, token)) {
        summary.failures.push_back(message);
    }
}

void requireNear(GoldenValidationSummary& summary,
                 double actual,
                 double expected,
                 double tolerance,
                 const char* message)
{
    if (std::fabs(actual - expected) > tolerance) {
        summary.failures.push_back(message);
    }
}

} // namespace

GoldenValidationSummary GoldenValidationSuite::validateResultsDirectory(const std::filesystem::path& resultsDirectory)
{
    GoldenValidationSummary summary {};

    std::string report {};
    std::string dc {};
    std::string ac {};
    std::string transient {};
    requireFile(summary, resultsDirectory / "u273_validation_report.json", "u273_validation_report.json", report);
    requireFile(summary, resultsDirectory / "u273_dc_global.json", "u273_dc_global.json", dc);
    requireFile(summary, resultsDirectory / "u273_ac_global.json", "u273_ac_global.json", ac);
    requireFile(summary, resultsDirectory / "u273_transient_attack_release.json", "u273_transient_attack_release.json", transient);

    if (!summary.failures.empty()) {
        return summary;
    }

    summary.dcScenarios = static_cast<int>(extractNumber(report, "dcScenarios").value_or(0.0));
    summary.acRows = static_cast<int>(extractNumber(report, "acRows").value_or(0.0));
    summary.transientRows = static_cast<int>(extractNumber(report, "transientRows").value_or(0.0));
    summary.transientCases = static_cast<int>(extractNumber(report, "transientCases").value_or(0.0));
    summary.firstDcCommandVolt = extractNumber(dc, "CMD").value_or(0.0);
    summary.firstDcBridgeNodeVolt = extractNumber(dc, "NB").value_or(0.0);
    summary.firstTransientMaxResidual = extractNumber(transient, "maxResidual").value_or(0.0);

    requireContains(summary, report, "PASS_WITH_GUARDED_BOUNDARIES", "golden report boundary changed");
    requireContains(summary, dc, "PARAMETRIC_THEVENIN_REFERENCE", "DC golden status changed");
    requireContains(summary, ac, "PARAMETRIC_THEVENIN_B6_BRIDGE_REFERENCE", "AC golden status changed");
    requireContains(summary, transient, "BOUNDED_QUASI_STATIC", "transient golden status changed");

    if (summary.dcScenarios != 4) {
        summary.failures.push_back("unexpected DC scenario count");
    }
    if (summary.acRows != 80) {
        summary.failures.push_back("unexpected AC row count");
    }
    if (summary.transientRows != 933) {
        summary.failures.push_back("unexpected transient row count");
    }
    if (summary.transientCases != 3) {
        summary.failures.push_back("unexpected transient case count");
    }

    requireNear(summary, summary.firstDcCommandVolt, 0.9773555249174533, 1.0e-9, "first DC CMD golden drift");
    requireNear(summary, summary.firstDcBridgeNodeVolt, 0.8263923585101612, 1.0e-9, "first DC NB golden drift");
    requireNear(summary, summary.firstTransientMaxResidual, 0.0, 1.0e-12, "first transient residual golden drift");

    return summary;
}

} // namespace u273::reference
