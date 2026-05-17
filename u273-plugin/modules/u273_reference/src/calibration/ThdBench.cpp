#include "u273/reference/calibration/ThdBench.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

#include "u273/reference/calibration/SonicTarget.h"
#include "u273/reference/state_space/StateSpaceSolver.h"

namespace u273::reference::calibration {

namespace {

namespace ss = u273::reference::state_space;

constexpr double kPi = 3.14159265358979323846;
constexpr int kHarmonicCount = 5;
constexpr int kWarmupSamples = 256;
constexpr double kThdFloorDb = -120.0;
// Guardrails matching the rest of the calibration stack: clamp arguments to
// std::exp through std::clamp before evaluation. ThdBench itself never calls
// std::exp directly because parameter -> circuit translation is done by the
// reference runner; the constant lives here only for documentation.
constexpr double kSafeExpClampHi = 60.0;
constexpr double kSafeExpClampLo = -60.0;

[[nodiscard]] double safeExp(double argument)
{
    const auto clamped = std::clamp(argument, kSafeExpClampLo, kSafeExpClampHi);
    return std::exp(clamped);
}

[[nodiscard]] ss::CircuitGraph applyParameters(ss::CircuitGraph circuit,
                                                const ActiveModelParameters& parameters)
{
    const auto saturationCurrent = safeExp(parameters.logIs.value);
    for (auto& diode : circuit.mutableDiodes()) {
        diode.model.saturationCurrentAmp = saturationCurrent;
        diode.model.gminSiemens = parameters.numericalGmin.value;
    }
    for (auto& bjt : circuit.mutableNpnBjts()) {
        bjt.model.saturationCurrentAmp = saturationCurrent;
        bjt.model.betaForward = parameters.betaForward.value;
        bjt.model.betaReverse = parameters.betaReverse.value;
        bjt.model.gminSiemens = parameters.numericalGmin.value;
    }
    return circuit;
}

[[nodiscard]] ss::NodeId sameNode(const ss::CircuitGraph& source,
                                  ss::CircuitGraph& target,
                                  ss::NodeId node)
{
    if (node.value <= 0) {
        return ss::kGroundNode;
    }
    return target.findNode(source.nodeName(node));
}

void copyNodes(const ss::CircuitGraph& source, ss::CircuitGraph& target)
{
    for (auto index = 1; index < source.nodeCount(); ++index) {
        const auto added = target.addNode(source.nodeName(ss::NodeId {index}));
        (void) added;
    }
}

// Cloning the entire CircuitGraph each sample is consistent with the offline
// dynamic transient case in B6B11CalibrationRunner.cpp: voltage sources are
// immutable through their accessors, so updating the drive voltage requires a
// fresh CircuitGraph. The cost is acceptable for a 4 k-sample offline bench.
[[nodiscard]] ss::CircuitGraph cloneWithCommandSourceVoltage(const ss::CircuitGraph& source,
                                                             const std::string& commandSourceId,
                                                             double voltage)
{
    ss::CircuitGraph target {};
    copyNodes(source, target);

    for (const auto& resistor : source.resistors()) {
        target.addResistor(resistor.id,
                           sameNode(source, target, resistor.positive),
                           sameNode(source, target, resistor.negative),
                           resistor.resistanceOhm);
    }
    for (const auto& capacitor : source.capacitors()) {
        target.addCapacitor(capacitor.id,
                            sameNode(source, target, capacitor.positive),
                            sameNode(source, target, capacitor.negative),
                            capacitor.capacitanceFarad);
    }
    for (const auto& currentSource : source.currentSources()) {
        target.addCurrentSource(currentSource.id,
                                sameNode(source, target, currentSource.positive),
                                sameNode(source, target, currentSource.negative),
                                currentSource.currentAmp);
    }
    for (const auto& sourceVoltage : source.voltageSources()) {
        const auto replacement = sourceVoltage.id == commandSourceId ? voltage : sourceVoltage.voltage;
        target.addVoltageSource(sourceVoltage.id,
                                sameNode(source, target, sourceVoltage.positive),
                                sameNode(source, target, sourceVoltage.negative),
                                replacement);
    }
    for (const auto& diode : source.diodes()) {
        target.addDiode(diode.id,
                        sameNode(source, target, diode.anode),
                        sameNode(source, target, diode.cathode),
                        diode.model);
    }
    for (const auto& bjt : source.npnBjts()) {
        target.addNpnBjt(bjt.id,
                         sameNode(source, target, bjt.collector),
                         sameNode(source, target, bjt.base),
                         sameNode(source, target, bjt.emitter),
                         bjt.model);
    }
    for (const auto& transformer : source.idealTransformerPorts()) {
        target.addIdealTransformerPort(transformer.id,
                                       sameNode(source, target, transformer.primaryPositive),
                                       sameNode(source, target, transformer.primaryNegative),
                                       sameNode(source, target, transformer.secondaryPositive),
                                       sameNode(source, target, transformer.secondaryNegative),
                                       transformer.turnsRatio,
                                       transformer.sourceResistanceOhm,
                                       transformer.loadResistanceOhm);
    }
    return target;
}

[[nodiscard]] std::string preferredCommandSourceId(const ss::CircuitGraph& circuit)
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id.find("B11") != std::string::npos) {
            return source.id;
        }
    }
    return circuit.voltageSources().empty() ? std::string {} : circuit.voltageSources().front().id;
}

[[nodiscard]] double dcBiasFor(const ss::CircuitGraph& circuit, const std::string& sourceId)
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id == sourceId) {
            return source.voltage;
        }
    }
    return 0.0;
}

[[nodiscard]] bool isSiemensBridgeExample(const std::string& measurementNode,
                                          const std::string& mode) noexcept
{
    return measurementNode == "diode_bridge_internal" && mode == "siemens_example";
}

[[nodiscard]] bool hasSiemensBridgeExampleConditions(const ThdBenchOptions& options) noexcept
{
    return std::isfinite(options.bridgeSignalAmplitudeVolt)
        && std::isfinite(options.bridgeControlVolt)
        && std::abs(options.bridgeSignalAmplitudeVolt - 0.025) <= 1.0e-9
        && std::abs(options.bridgeControlVolt - 1.0) <= 1.0e-9;
}

// Single-bin Goertzel coefficient/state running over the captured samples.
// Each harmonic uses three accumulators (s0/s1/s2): no per-sample
// allocation, no STL container in the inner loop.
struct GoertzelBin {
    double cosOmega {};
    double sinOmega {};
    double coeff {};
    double s1 {};
    double s2 {};

    void reset() noexcept
    {
        s1 = 0.0;
        s2 = 0.0;
    }

    void push(double sample) noexcept
    {
        const auto s0 = sample + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    [[nodiscard]] double amplitude(int sampleCount) const noexcept
    {
        const auto real = s1 - s2 * cosOmega;
        const auto imag = s2 * sinOmega;
        const auto magnitude = std::sqrt(real * real + imag * imag);
        return sampleCount > 0 ? magnitude * 2.0 / static_cast<double>(sampleCount) : 0.0;
    }
};

} // namespace

ThdBenchResult ThdBench::evaluate(const ActiveModelParameters& parameters,
                                   const CalibrationDataset& dataset,
                                   const ss::CircuitGraph& referenceCircuit,
                                   const ThdBenchOptions& options) const
{
    ThdBenchResult result {};
    result.toleranceDb = options.thdToleranceDb;
    result.measuredThdDb = kThdFloorDb;
    result.measurementNode = options.measurementNode;
    result.mode = options.mode;

    bool hasComparableTarget = false;
    if (options.useSonicTargetMatrix) {
        const auto targetPath = options.sonicTargetCsvPath.empty()
            ? defaultSonicThdTargetCsvPath()
            : options.sonicTargetCsvPath;
        const auto targetMatrix = loadSonicThdTargetCsv(targetPath);
        if (!targetMatrix.loaded) {
            result.targetStatus = "target_load_failed";
            for (const auto& failure : targetMatrix.failures) {
                result.failures.push_back(failure);
            }
        } else if (const auto* row = findThdTargetByNodeAndMode(
                       targetMatrix.rows,
                       options.measurementNode,
                       options.mode)) {
            if (isSiemensBridgeExample(options.measurementNode, options.mode)
                && !hasSiemensBridgeExampleConditions(options)) {
                result.targetStatus = "unmapped";
                result.failures.push_back("Siemens internal bridge THD target requires 25 mV bridge signal and 1 V control");
            } else {
                result.targetStatus = "mapped";
                result.targetId = row->targetId;
                result.targetSourceType = row->sourceType;
                result.targetConfidence = row->confidence;
                result.targetThdPercent = row->thdTargetPercent;
                result.targetThdMaxPercent = row->thdMaxPercent;
                result.goldenThdDb = row->hasFiniteCeilingPercent()
                    ? thdPercentToDb(row->thdMaxPercent)
                    : row->thdTargetDb;
                hasComparableTarget = std::isfinite(result.goldenThdDb);
                if (!hasComparableTarget) {
                    result.failures.push_back("mapped THD target has no finite dB ceiling");
                }
            }
        } else {
            result.targetStatus = "unmapped";
            result.failures.push_back("no comparable sonic THD target for measurement_node="
                                      + options.measurementNode
                                      + ", mode="
                                      + options.mode);
        }
    } else if (options.allowLegacyGoldenFallback) {
        result.targetStatus = "legacy_scalar";
        result.goldenThdDb = options.goldenThdDb;
        hasComparableTarget = std::isfinite(result.goldenThdDb);
        if (!hasComparableTarget) {
            result.failures.push_back("legacy scalar THD target is non-finite");
        }
    } else {
        result.targetStatus = "disabled";
        result.failures.push_back("THD sonic target matrix disabled without legacy fallback");
    }

    const auto inputsValid =
        parameters.isValid()
        && dataset.isValid()
        && options.sampleRate > 0.0
        && options.testFrequencyHz > 0.0
        && options.testFrequencyHz * 2.0 * kHarmonicCount < options.sampleRate
        && options.lengthSamples > kWarmupSamples
        && std::isfinite(options.inputAmplitudeDbfs)
        && std::isfinite(options.thdToleranceDb)
        && referenceCircuit.nodeCount() > 1
        && !referenceCircuit.voltageSources().empty();
    if (!inputsValid) {
        result.failures.push_back("invalid THD bench input");
        return result;
    }

    const auto commandSourceId = preferredCommandSourceId(referenceCircuit);
    if (commandSourceId.empty()) {
        result.failures.push_back("THD bench could not locate a command voltage source");
        return result;
    }
    const auto outputNode = referenceCircuit.findNode("CMD");
    if (outputNode.value <= 0 || outputNode.value >= referenceCircuit.nodeCount()) {
        result.failures.push_back("THD bench could not locate the CMD output node");
        return result;
    }
    const auto outputIndex = static_cast<std::size_t>(outputNode.value - 1);

    const auto calibratedCircuit = applyParameters(referenceCircuit, parameters);
    const auto dcBias = dcBiasFor(calibratedCircuit, commandSourceId);
    const auto amplitudeNorm = std::pow(10.0, options.inputAmplitudeDbfs / 20.0);

    ss::SolverOptions solverOptions {};
    solverOptions.sampleRate = options.sampleRate;
    solverOptions.method = ss::IntegrationMethod::trBdf2;
    if (!solverOptions.isValid()) {
        result.failures.push_back("THD bench solver options invalid");
        return result;
    }

    // The bench is now committed: every later branch must populate a finite
    // measuredThdDb / passed pair without throwing. validInput = true here so
    // the caller can distinguish "did not run" from "ran but failed".
    result.validInput = true;

    // Per-harmonic Goertzel state. Three doubles state + 3 doubles precomputed
    // constants per bin, allocated once on the stack.
    std::array<GoertzelBin, kHarmonicCount> bins {};
    for (int k = 0; k < kHarmonicCount; ++k) {
        const auto omega = 2.0 * kPi * static_cast<double>(k + 1)
            * options.testFrequencyHz / options.sampleRate;
        bins[k].cosOmega = std::cos(omega);
        bins[k].sinOmega = std::sin(omega);
        bins[k].coeff = 2.0 * bins[k].cosOmega;
        bins[k].reset();
    }

    auto initialState = ss::CircuitState {};
    {
        ss::StateSpaceSolver stateFactory {calibratedCircuit};
        initialState = stateFactory.createInitialState();
    }
    if (!initialState.isValidFor(calibratedCircuit)) {
        result.failures.push_back("THD bench initial state inconsistent with calibrated circuit");
        return result;
    }

    auto state = initialState;
    int convergedSamples = 0;
    int goertzelSamples = 0;

    for (int index = 0; index < options.lengthSamples; ++index) {
        const auto phase = 2.0 * kPi * options.testFrequencyHz
            * static_cast<double>(index) / options.sampleRate;
        const auto drive = dcBias + amplitudeNorm * std::sin(phase);
        const auto stepCircuit = cloneWithCommandSourceVoltage(calibratedCircuit,
                                                                commandSourceId,
                                                                drive);
        ss::StateSpaceSolver solver {stepCircuit};
        const auto step = solver.step(state, solverOptions);
        if (!step.validInput || !step.converged) {
            // One non-converged sample is enough to invalidate THD; bail out
            // with the worst-case THD figure but keep validInput == true so
            // the audio gate can read the failure list. Never throws.
            result.failures.push_back("THD bench solver step " + std::to_string(index)
                                       + " did not converge");
            result.measuredThdDb = std::numeric_limits<double>::quiet_NaN();
            return result;
        }
        ++convergedSamples;

        // Discard warmup samples to let DC transients settle; only after that
        // do we feed the Goertzel accumulators.
        if (index < kWarmupSamples) {
            continue;
        }

        if (outputIndex >= state.unknowns.size()) {
            result.failures.push_back("THD bench captured state has no CMD entry");
            result.measuredThdDb = std::numeric_limits<double>::quiet_NaN();
            return result;
        }
        const auto sample = state.unknowns[outputIndex] - dcBias;
        if (!std::isfinite(sample)) {
            result.failures.push_back("THD bench captured non-finite CMD sample at " + std::to_string(index));
            result.measuredThdDb = std::numeric_limits<double>::quiet_NaN();
            return result;
        }
        for (int k = 0; k < kHarmonicCount; ++k) {
            bins[k].push(sample);
        }
        ++goertzelSamples;
    }

    if (goertzelSamples <= 0) {
        result.failures.push_back("THD bench produced no analysis samples");
        result.measuredThdDb = std::numeric_limits<double>::quiet_NaN();
        return result;
    }
    (void) convergedSamples;

    const auto a1 = bins[0].amplitude(goertzelSamples);
    double harmonicSquared {};
    for (int k = 1; k < kHarmonicCount; ++k) {
        const auto ak = bins[k].amplitude(goertzelSamples);
        if (!std::isfinite(ak)) {
            result.failures.push_back("non-finite Goertzel amplitude at harmonic "
                                       + std::to_string(k + 1));
            result.measuredThdDb = std::numeric_limits<double>::quiet_NaN();
            return result;
        }
        harmonicSquared += ak * ak;
    }
    if (!std::isfinite(a1) || a1 <= 0.0) {
        // Either the fundamental never developed or the model is numerically
        // silent; both are catastrophic for THD measurement.
        result.failures.push_back("THD bench fundamental amplitude is non-positive");
        result.measuredThdDb = kThdFloorDb;
        result.passed = false;
        return result;
    }

    const auto thdLinear = std::sqrt(harmonicSquared) / a1;
    if (!std::isfinite(thdLinear)) {
        result.failures.push_back("non-finite THD ratio");
        result.measuredThdDb = std::numeric_limits<double>::quiet_NaN();
        return result;
    }
    if (thdLinear < 1.0e-12) {
        result.measuredThdDb = kThdFloorDb;
    } else {
        result.measuredThdDb = 20.0 * std::log10(thdLinear);
    }

    // Pass criterion: measured THD must lie at or below the mapped target
    // ceiling + tolerance. If the measurement point has no scientifically
    // comparable row in the target matrix, the bench still reports the
    // measurement but keeps the audio gate closed.
    if (!hasComparableTarget) {
        result.passed = false;
        return result;
    }
    if (std::isfinite(result.measuredThdDb) && std::isfinite(result.goldenThdDb)) {
        result.passed = result.measuredThdDb <= result.goldenThdDb + result.toleranceDb;
        if (!result.passed) {
            result.failures.push_back("measured THD exceeds golden + tolerance");
        }
    } else {
        result.passed = false;
        result.failures.push_back("THD comparison produced non-finite figures");
    }

    return result;
}

} // namespace u273::reference::calibration
