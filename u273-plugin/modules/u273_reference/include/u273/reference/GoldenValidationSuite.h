#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace u273::reference {

// Pinned summary of generated JS golden files consumed by C++ tests.
struct GoldenValidationSummary {
    int dcScenarios {};
    int acRows {};
    int transientRows {};
    int transientCases {};
    double firstDcCommandVolt {};
    double firstDcBridgeNodeVolt {};
    double firstTransientMaxResidual {};
    std::vector<std::string> failures {};

    [[nodiscard]] bool passed() const noexcept { return failures.empty(); }
};

// File-level validation bridge between solver outputs in results/ and the C++
// reference test suite.
class GoldenValidationSuite {
public:
    [[nodiscard]] static GoldenValidationSummary validateResultsDirectory(const std::filesystem::path& resultsDirectory);
};

} // namespace u273::reference
