#pragma once

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "u273/reference/calibration/ActiveModelParameters.h"
#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::calibration {

// Offline THD bench. Drives the calibrated full-active model with a small
// pure-tone test signal injected through the B11/command voltage source and
// extracts the first five harmonic amplitudes from the CMD output node using
// a Goertzel filter. The result is intentionally scientific: the bench
// returns measured THD and the failure list, never throws, never allocates
// inside the per-sample loop.
struct ThdBenchOptions {
    double testFrequencyHz {1000.0};
    double sampleRate {96000.0};
    int lengthSamples {4096};
    double inputAmplitudeDbfs {-6.0};
    double thdToleranceDb {0.5};
    // Legacy scalar ceiling. It is kept only for explicit fallback tests; the
    // production offline runner uses the sonic target matrix below.
    double goldenThdDb {-60.0};
    bool useSonicTargetMatrix {true};
    bool allowLegacyGoldenFallback {};
    std::filesystem::path sonicTargetCsvPath {};
    // Current THD bench drives the B11 command source and captures CMD. That
    // is an internal numerical probe, not the published device output THD.
    std::string measurementNode {"cmd_internal"};
    std::string mode {"b11_cmd_probe"};
    // Required only for the Siemens internal diode-bridge example. That row is
    // comparable solely at 25 mV bridge signal and 1 V control.
    double bridgeSignalAmplitudeVolt {std::numeric_limits<double>::quiet_NaN()};
    double bridgeControlVolt {std::numeric_limits<double>::quiet_NaN()};
};

struct ThdBenchResult {
    bool validInput {false};
    bool passed {false};
    double measuredThdDb {0.0};
    double goldenThdDb {std::numeric_limits<double>::quiet_NaN()};
    double toleranceDb {0.5};
    std::string targetStatus {"not_evaluated"};
    std::string targetId {};
    std::string measurementNode {};
    std::string mode {};
    std::string targetSourceType {};
    std::string targetConfidence {};
    double targetThdPercent {std::numeric_limits<double>::quiet_NaN()};
    double targetThdMaxPercent {std::numeric_limits<double>::quiet_NaN()};
    std::vector<std::string> failures {};
};

class ThdBench {
public:
    // Synthesises a sine tone, drives the calibrated reference circuit via
    // the B11 (or first available) voltage source, captures CMD node samples
    // and reports a Goertzel-derived THD figure.
    [[nodiscard]] ThdBenchResult evaluate(
        const ActiveModelParameters& parameters,
        const CalibrationDataset& dataset,
        const u273::reference::state_space::CircuitGraph& referenceCircuit,
        const ThdBenchOptions& options = {}) const;
};

} // namespace u273::reference::calibration
