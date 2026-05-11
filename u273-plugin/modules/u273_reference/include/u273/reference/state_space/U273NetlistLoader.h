#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "u273/core/ModelBoundary.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::state_space {

enum class BjtStampPolicy {
    keepGuarded = 0,
    stampKnownTerminals
};

enum class U273NetlistSource {
    componentInventory = 0,
    dcExecutionReference
};

// Loader knobs for experiments. The default path keeps active devices guarded
// unless a test explicitly requests known-terminal BJT stamping.
struct U273NetlistLoaderOptions {
    U273NetlistSource source {U273NetlistSource::componentInventory};
    double potentiometerWiperFraction {0.5};
    bool addNumericalNodeGmin {true};
    double nodeGminResistanceOhm {1.0e12};
    BjtStampPolicy bjtStampPolicy {BjtStampPolicy::keepGuarded};
};

// Audit trail for generated netlists: what was stamped, what stayed guarded,
// and whether the circuit can be used for offline solving.
struct U273NetlistLoadReport {
    std::string sourcePath {};
    std::string status {};
    std::string scientificBoundary {};
    int componentObjects {};
    int stampedResistors {};
    int stampedCapacitors {};
    int stampedVoltageSources {};
    int stampedCurrentSources {};
    int stampedDiodes {};
    int stampedBjts {};
    int guardedActiveDevices {};
    int potentiometersSplit {};
    int switchComponents {};
    int transformerBoundaries {};
    int zeroValueComponents {};
    int unresolvedComponents {};
    int numericalGminResistors {};
    std::vector<std::string> unresolvedIds {};

    [[nodiscard]] bool hasUsableCircuit() const noexcept;
    [[nodiscard]] bool isFullActiveClosure() const noexcept;
    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept;
};

struct U273LoadedNetlist {
    CircuitGraph circuit {};
    U273NetlistLoadReport report {};
};

// Converts the generated JS netlist JSON into a CircuitGraph plus an audit
// report. It does not claim scientific closure by itself.
class U273NetlistLoader {
public:
    [[nodiscard]] static U273LoadedNetlist loadFromFile(
        const std::filesystem::path& path,
        const U273NetlistLoaderOptions& options = {});
};

} // namespace u273::reference::state_space
