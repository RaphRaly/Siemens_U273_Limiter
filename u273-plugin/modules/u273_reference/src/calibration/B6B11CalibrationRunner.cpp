#include "u273/reference/calibration/B6B11CalibrationRunner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "u273/reference/calibration/ActiveTopologyEvaluator.h"
#include "u273/reference/calibration/BoundedCalibrationSolver.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/calibration/IdentifiabilityAnalyzer.h"
#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/ThdBench.h"
#include "u273/reference/state_space/U273NetlistLoader.h"
#include "u273/reference/state_space/U273ReferenceCircuitBuilder.h"

namespace u273::reference::calibration {

namespace {

namespace ss = u273::reference::state_space;

void appendGateNotes(CalibrationReport& report, const ResidualGateResult& gate)
{
    report.notes.push_back(gate.gateName + (gate.passed ? " passed" : " did not pass"));
    for (const auto& failure : gate.failures) {
        report.notes.push_back(gate.gateName + ": " + failure);
    }
    for (const auto& set : gate.sets) {
        for (const auto& failure : set.failures) {
            report.notes.push_back(gate.gateName + "/" + set.name + ": " + failure);
        }
        for (const auto& note : set.notes) {
            report.notes.push_back(gate.gateName + "/" + set.name + ": " + note);
        }
    }
}

[[nodiscard]] std::vector<double> acFrequenciesOrFallback(const CalibrationDataset& dataset)
{
    if (!dataset.acFrequenciesHz.empty()) {
        return dataset.acFrequenciesHz;
    }

    std::vector<double> frequencies {};
    for (const auto& point : dataset.acPoints) {
        const auto duplicate = std::find_if(frequencies.begin(), frequencies.end(), [&point](double existing) {
            return std::abs(existing - point.frequencyHz) < 1.0e-9;
        });
        if (duplicate == frequencies.end()) {
            frequencies.push_back(point.frequencyHz);
        }
    }
    return frequencies;
}

[[nodiscard]] std::string firstVoltageSourceId(const ss::CircuitGraph& circuit)
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id.find("B11") != std::string::npos) {
            return source.id;
        }
    }
    return circuit.voltageSources().empty() ? std::string {} : circuit.voltageSources().front().id;
}

[[nodiscard]] std::string formatDbForNote(double value)
{
    if (!std::isfinite(value)) {
        return "n/a";
    }
    std::ostringstream s;
    s << value << "dB";
    return s.str();
}

[[nodiscard]] std::string methodName(ss::IntegrationMethod method)
{
    switch (method) {
    case ss::IntegrationMethod::backwardEuler:
        return "backwardEuler";
    case ss::IntegrationMethod::trapezoidal:
        return "trapezoidal";
    case ss::IntegrationMethod::implicitMidpoint:
        return "implicitMidpoint";
    case ss::IntegrationMethod::trBdf2:
        return "trBdf2";
    }
    return "unknown";
}

[[nodiscard]] ss::NodeId sameNode(const ss::CircuitGraph& source, ss::CircuitGraph& target, ss::NodeId node)
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

[[nodiscard]] double nodeVoltageOrNan(const ss::CircuitGraph& circuit,
                                      const ss::CircuitState& state,
                                      ss::NodeId node)
{
    if (node.value <= 0 || node.value >= circuit.nodeCount()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto index = static_cast<std::size_t>(node.value - 1);
    if (index >= state.unknowns.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return state.unknowns[index];
}

[[nodiscard]] double nodeVoltageOrNan(const ss::CircuitGraph& circuit,
                                      const ss::CircuitState& state,
                                      std::string_view nodeName)
{
    const auto node = circuit.findNode(nodeName);
    return nodeVoltageOrNan(circuit, state, node);
}

void setBridgeDiodeConductance(ss::B6BridgeSmallSignalAcOptions& options,
                               const std::string& diodeId,
                               double conductanceSiemens)
{
    if (!std::isfinite(conductanceSiemens) || conductanceSiemens <= 0.0) {
        return;
    }

    if (diodeId == "D1_OA154Q") {
        options.d1ConductanceSiemens = conductanceSiemens;
    } else if (diodeId == "D2_SSD55") {
        options.d2ConductanceSiemens = conductanceSiemens;
    } else if (diodeId == "D3_SSD55") {
        options.d3ConductanceSiemens = conductanceSiemens;
    } else if (diodeId == "D4_OA154Q") {
        options.d4ConductanceSiemens = conductanceSiemens;
    }
}

[[nodiscard]] ss::B6BridgeSmallSignalAcOptions buildB6AcOptionsFromOperatingPoint(
    const ss::CircuitGraph& dcCircuit,
    const ss::CircuitState& state,
    double commandPortResistanceOhm)
{
    ss::B6BridgeSmallSignalAcOptions options {};
    options.commandPortResistanceOhm = commandPortResistanceOhm;

    for (const auto& diode : dcCircuit.diodes()) {
        const auto anodeVolt = nodeVoltageOrNan(dcCircuit, state, diode.anode);
        const auto cathodeVolt = nodeVoltageOrNan(dcCircuit, state, diode.cathode);
        if (!std::isfinite(anodeVolt) || !std::isfinite(cathodeVolt)) {
            continue;
        }

        setBridgeDiodeConductance(options,
                                  diode.id,
                                  diode.model.conductanceSiemens(anodeVolt - cathodeVolt));
    }

    return options;
}

[[nodiscard]] OperatingPointResult makeSolvedLinearOperatingPoint(const ss::CircuitGraph& circuit)
{
    const ss::StateSpaceSolver stateFactory {circuit};
    OperatingPointResult op {};
    op.validInput = true;
    op.converged = true;
    op.state = stateFactory.createInitialState();
    return op;
}

[[nodiscard]] AcLinearizationResult solveB6SmallSignalAcReference(const ss::CircuitGraph& dcCircuit,
                                                                  const OperatingPointResult& dcOperatingPoint,
                                                                  double commandPortResistanceOhm,
                                                                  const std::vector<double>& frequencies)
{
    const auto acOptions = buildB6AcOptionsFromOperatingPoint(dcCircuit,
                                                             dcOperatingPoint.state,
                                                             commandPortResistanceOhm);
    const auto acCircuit = ss::U273ReferenceCircuitBuilder::buildB6BridgeSmallSignalAcReference(acOptions);
    const auto output = acCircuit.findNode("CMD");
    if (output.value <= 0) {
        AcLinearizationResult failed {};
        failed.failures.push_back("B6 AC reference CMD output node was not found");
        return failed;
    }

    AcSourcePort source {};
    source.voltageSourceId = "VAC";
    source.outputNode = output;
    source.amplitudeVolt = acOptions.sourceAmplitudeVolt;

    const LinearizedAcSolver acSolver {};
    return acSolver.solveSmallSignal(acCircuit,
                                     makeSolvedLinearOperatingPoint(acCircuit),
                                     source,
                                     frequencies);
}

struct DynamicTransientCaseResult {
    TransientModelSummary summary {};
    TransientRunDiagnostics diagnostics {};
    std::vector<std::string> failures {};
};

void updateDynamicDiagnostics(TransientRunDiagnostics& diagnostics, const ss::StepResult& step)
{
    diagnostics.maxResidualNorm = std::max(diagnostics.maxResidualNorm, step.residualNorm);
    diagnostics.maxDeltaNorm = std::max(diagnostics.maxDeltaNorm, step.deltaNorm);
    ++diagnostics.totalSolverSteps;
}

[[nodiscard]] bool runDynamicStepWithRetries(const ss::CircuitGraph& circuit,
                                             ss::CircuitState& state,
                                             const ss::SolverOptions& options,
                                             int logicalStepIndex,
                                             TransientRunDiagnostics& diagnostics,
                                             std::vector<std::string>& failures)
{
    ss::StateSpaceSolver solver {circuit};
    const auto previousState = state;
    const auto step = solver.step(state, options);
    updateDynamicDiagnostics(diagnostics, step);
    if (step.validInput && step.converged) {
        ++diagnostics.steps;
        return true;
    }

    if (diagnostics.firstFailedStep < 0) {
        diagnostics.firstFailedStep = logicalStepIndex;
    }

    constexpr std::array<int, 3> retryFactors {2, 4, 8};
    for (const auto factor : retryFactors) {
        auto retryState = previousState;
        auto retryOptions = options;
        retryOptions.sampleRate = options.sampleRate * static_cast<double>(factor);
        auto allSubstepsConverged = retryOptions.isValid();

        for (auto substep = 0; allSubstepsConverged && substep < factor; ++substep) {
            const auto retryStep = solver.step(retryState, retryOptions);
            updateDynamicDiagnostics(diagnostics, retryStep);
            allSubstepsConverged = retryStep.validInput && retryStep.converged;
        }

        if (allSubstepsConverged) {
            state = std::move(retryState);
            ++diagnostics.retries;
            ++diagnostics.steps;
            return true;
        }
    }

    failures.push_back("dynamic transient step " + std::to_string(logicalStepIndex)
                       + " did not converge, including substep retries");
    return false;
}

struct TransientSample {
    double timeSeconds {};
    double driveVolt {};
    double cmdVolt {};
};

void finishTransientSummary(TransientModelSummary& summary, const std::vector<TransientSample>& samples)
{
    if (samples.empty()) {
        return;
    }

    summary.hasMaxDriveVolt = true;
    summary.maxDriveVolt = samples.front().driveVolt;
    summary.hasMaxCmdVolt = true;
    summary.maxCmdVolt = samples.front().cmdVolt;
    auto peakIndex = std::size_t {};

    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        if (sample.driveVolt > summary.maxDriveVolt) {
            summary.maxDriveVolt = sample.driveVolt;
        }
        if (sample.cmdVolt > summary.maxCmdVolt) {
            summary.maxCmdVolt = sample.cmdVolt;
            peakIndex = index;
        }
    }

    const auto attackThreshold = summary.maxCmdVolt * 0.9;
    for (const auto& sample : samples) {
        if (sample.cmdVolt >= attackThreshold) {
            summary.hasAttack90TimeSeconds = true;
            summary.attack90TimeSeconds = sample.timeSeconds;
            break;
        }
    }

    const auto releaseThreshold = summary.maxCmdVolt * 0.1;
    for (auto index = peakIndex; index < samples.size(); ++index) {
        if (samples[index].cmdVolt <= releaseThreshold) {
            summary.hasRelease10TimeSeconds = true;
            summary.release10TimeSeconds = samples[index].timeSeconds;
            break;
        }
    }
}

[[nodiscard]] DynamicTransientCaseResult runDynamicTransientCase(
    const TransientGoldenSummary& golden,
    const std::vector<TransientGoldenRow>& rows,
    const ss::CircuitGraph& dcReferenceCircuit,
    const ss::SolverOptions& options,
    double driveScale)
{
    DynamicTransientCaseResult result {};
    result.summary.caseName = golden.caseName;
    result.diagnostics.caseName = golden.caseName;
    result.diagnostics.method = methodName(options.method);
    result.diagnostics.sampleRate = options.sampleRate;

    const auto sourceId = firstVoltageSourceId(dcReferenceCircuit);
    if (sourceId.empty()) {
        result.failures.push_back("dynamic transient has no command voltage source");
        return result;
    }
    if (rows.empty()) {
        result.failures.push_back("dynamic transient has no golden rows for case " + golden.caseName);
        return result;
    }
    if (!options.isValid()) {
        result.failures.push_back("dynamic transient options are invalid");
        return result;
    }

    const auto firstCircuit = cloneWithCommandSourceVoltage(dcReferenceCircuit,
                                                           sourceId,
                                                           rows.front().driveVolt * driveScale);
    ss::StateSpaceSolver stateFactory {firstCircuit};
    auto state = stateFactory.createInitialState();
    std::vector<TransientSample> samples {};
    samples.reserve(rows.size());

    auto currentTime = rows.front().timeSeconds;
    auto logicalStep = 0;
    const auto dynamicCircuitHasCapacitors = !dcReferenceCircuit.capacitors().empty();

    for (const auto& row : rows) {
        const auto drive = row.driveVolt * driveScale;
        const auto circuit = cloneWithCommandSourceVoltage(dcReferenceCircuit, sourceId, drive);
        const auto deltaTime = std::max(0.0, row.timeSeconds - currentTime);
        auto substeps = 1;
        if (dynamicCircuitHasCapacitors && deltaTime > 0.0) {
            substeps = std::max(1, static_cast<int>(std::ceil(deltaTime * options.sampleRate)));
        }

        auto stepOptions = options;
        if (deltaTime > 0.0 && dynamicCircuitHasCapacitors) {
            stepOptions.sampleRate = static_cast<double>(substeps) / deltaTime;
        }

        for (auto substep = 0; substep < substeps; ++substep) {
            if (!runDynamicStepWithRetries(circuit,
                                           state,
                                           stepOptions,
                                           logicalStep,
                                           result.diagnostics,
                                           result.failures)) {
                return result;
            }
            ++logicalStep;
        }

        const auto cmd = nodeVoltageOrNan(circuit, state, "CMD");
        if (std::isfinite(cmd)) {
            samples.push_back(TransientSample {row.timeSeconds, drive, cmd});
        }
        currentTime = row.timeSeconds;
    }

    result.diagnostics.converged = result.failures.empty();
    result.diagnostics.convergenceRatio = result.diagnostics.totalSolverSteps > 0
        ? static_cast<double>(result.diagnostics.steps)
            / static_cast<double>(result.diagnostics.totalSolverSteps)
        : 0.0;
    finishTransientSummary(result.summary, samples);
    return result;
}

[[nodiscard]] std::vector<TransientModelSummary> buildDynamicTransientSummaries(
    const CalibrationDataset& dataset,
    const ss::CircuitGraph& dcReferenceCircuit,
    const ss::SolverOptions& options,
    std::vector<TransientRunDiagnostics>& diagnostics,
    std::vector<std::string>& failures)
{
    std::vector<TransientModelSummary> summaries {};
    summaries.reserve(dataset.transientSummaries.size());
    for (const auto& golden : dataset.transientSummaries) {
        const auto rows = dataset.transientRowsForCase(golden.caseName);
        auto run = runDynamicTransientCase(golden, rows, dcReferenceCircuit, options, 1.0);
        diagnostics.push_back(run.diagnostics);
        for (const auto& failure : run.failures) {
            failures.push_back(golden.caseName + ": " + failure);
        }
        summaries.push_back(std::move(run.summary));
    }

    return summaries;
}

void appendGuardedTopologyReasons(CalibrationReport& report, const ResidualGateResult& diagnostic)
{
    auto addedPointReason = false;
    for (const auto& set : diagnostic.sets) {
        for (const auto& point : set.points) {
            if (!point.failed()) {
                continue;
            }

            std::ostringstream message {};
            message << "guarded component inventory DC mismatch at " << point.name
                    << ": golden=" << point.measured << point.unit
                    << ", model=" << point.model << point.unit
                    << ", error=" << point.rawError << point.unit
                    << ", weighted=" << point.weightedError;
            report.rejectedTopologyReasons.push_back(message.str());
            addedPointReason = true;
        }
    }

    if (!addedPointReason && !diagnostic.passed) {
        report.rejectedTopologyReasons.push_back("guarded component inventory DC diagnostic did not pass");
    }
}

} // namespace

CalibrationReport B6B11CalibrationRunner::createInitialReport(const CalibrationDataset& dataset,
                                                              const OperatingPointResult& operatingPoint) const
{
    CalibrationReport report {};
    report.boundary = u273::core::ModelBoundary::fullActiveModelUnverified;

    if (!dataset.isValid()) {
        report.notes.push_back("calibration dataset is not valid");
        return report;
    }
    if (!operatingPoint.converged) {
        report.notes.push_back("DC operating point has not converged");
        return report;
    }

    report.gates.dc = true;
    report.gates.referenceDc = true;
    report.notes.push_back("DC dataset and operating point are available; AC/transient/audio/identifiability gates remain open");
    return report;
}

CalibrationReport B6B11CalibrationRunner::runOffline(const std::filesystem::path& resultsDirectory,
                                                     const OfflineCalibrationRunOptions& options) const
{
    CalibrationReport report {};
    report.boundary = u273::core::ModelBoundary::fullActiveModelUnverified;

    const auto dataset = CalibrationDataset::loadFromResultsDirectory(resultsDirectory);
    if (!dataset.isValid()) {
        report.notes.push_back("calibration dataset is not valid");
        for (const auto& failure : dataset.failures) {
            report.notes.push_back("dataset: " + failure);
        }
        return report;
    }

    const auto netlistPath = resultsDirectory / "u273_netlist.json";

    ss::U273NetlistLoaderOptions dcReferenceLoaderOptions {};
    dcReferenceLoaderOptions.source = ss::U273NetlistSource::dcExecutionReference;
    const auto dcReference = ss::U273NetlistLoader::loadFromFile(netlistPath, dcReferenceLoaderOptions);
    if (!dcReference.report.hasUsableCircuit()) {
        report.notes.push_back("DC execution reference loader did not produce a usable circuit");
        return report;
    }

    const auto dcView = DCCircuitView::fromCircuit(dcReference.circuit);
    const OperatingPointSolver opSolver {options.operatingPoint};
    const auto op = opSolver.solve(dcView);
    if (!op.converged) {
        report.notes.push_back("DC execution reference operating point has not converged");
        for (const auto& failure : op.failures) {
            report.notes.push_back("OP: " + failure);
        }
        return report;
    }

    const auto dcGate = evaluateDcResiduals(dataset, dcView.circuit, op);
    report.gates.dc = dcGate.passed;
    report.gates.referenceDc = dcGate.passed;
    appendGateNotes(report, dcGate);

    const auto guarded = ss::U273NetlistLoader::loadFromFile(netlistPath);
    DCCircuitView guardedDcView {};
    OperatingPointResult guardedOp {};
    auto guardedOpConverged = false;
    if (!guarded.report.hasUsableCircuit()) {
        report.rejectedTopologyReasons.push_back("guarded component inventory loader did not produce a usable circuit");
    } else {
        guardedDcView = DCCircuitView::fromCircuit(guarded.circuit);
        guardedOp = opSolver.solve(guardedDcView);
        guardedOpConverged = guardedOp.converged;
        if (guardedOpConverged) {
            const auto guardedDiagnostic = evaluateDcResiduals(dataset, guardedDcView.circuit, guardedOp);
            report.gates.guardedTopologyDiagnostic = guardedDiagnostic.passed;
            if (!guardedDiagnostic.passed) {
                appendGuardedTopologyReasons(report, guardedDiagnostic);
            }
        } else {
            report.rejectedTopologyReasons.push_back("guarded component inventory DC operating point has not converged");
            for (const auto& failure : guardedOp.failures) {
                report.notes.push_back("guarded OP: " + failure);
            }
        }
    }

    AcResidualOptions acOptions {};
    acOptions.scenario = "rsource_10k";
    acOptions.driveVolt = 1.0;
    acOptions.matchDriveVolt = true;
    acOptions.commandSourceOhm = 10000.0;
    acOptions.matchCommandSourceOhm = true;
    const auto ac = solveB6SmallSignalAcReference(dcReference.circuit,
                                                  op,
                                                  acOptions.commandSourceOhm,
                                                  acFrequenciesOrFallback(dataset));
    const auto acGate = evaluateAcResiduals(dataset, ac, acOptions);
    report.gates.ac = acGate.passed;
    report.gates.referenceAc = acGate.passed;
    appendGateNotes(report, acGate);
    for (const auto& failure : ac.failures) {
        report.notes.push_back("AC solver: " + failure);
    }

    std::vector<std::string> transientFailures {};
    const auto transientSummaries = buildDynamicTransientSummaries(dataset,
                                                                   dcReference.circuit,
                                                                   options.transient,
                                                                   report.transientDiagnostics,
                                                                   transientFailures);
    for (const auto& diagnostic : report.transientDiagnostics) {
        report.notes.push_back("dynamic transient " + diagnostic.caseName
                               + ": method=" + diagnostic.method
                               + ", sampleRate=" + std::to_string(diagnostic.sampleRate)
                               + ", steps=" + std::to_string(diagnostic.steps)
                               + ", solverSteps=" + std::to_string(diagnostic.totalSolverSteps)
                               + ", retries=" + std::to_string(diagnostic.retries)
                               + ", firstFailedStep=" + std::to_string(diagnostic.firstFailedStep)
                               + ", maxResidual=" + std::to_string(diagnostic.maxResidualNorm)
                               + ", convergenceRatio=" + std::to_string(diagnostic.convergenceRatio));
    }
    for (const auto& failure : transientFailures) {
        report.notes.push_back("dynamic transient: " + failure);
    }
    const auto transientGate = evaluateTransientResiduals(dataset, transientSummaries);
    report.gates.transient = transientGate.passed && transientFailures.empty();
    report.gates.referenceTransient = report.gates.transient;
    appendGateNotes(report, transientGate);

    if (guardedOpConverged) {
        const ActiveTopologyEvaluator topologyEvaluator {};
        ActiveTopologyEvaluationOptions topologyOptions {};
        topologyOptions.requireAcImprovement = true;
        const auto topologyResults = topologyEvaluator.evaluateB6GuardedCandidates(
            guardedDcView.circuit,
            guardedOp,
            topologyOptions);
        for (const auto& topology : topologyResults) {
            std::ostringstream message {};
            message << "guarded topology candidate " << topology.candidate.id
                    << ": " << topology.reason
                    << " (VBE=" << topology.vbeVolt
                    << " V, VCE=" << topology.vceVolt
                    << " V, localKCL=" << topology.localKclAmp << " A)";
            if (topology.accepted) {
                report.notes.push_back(message.str());
            } else {
                report.rejectedTopologyReasons.push_back(message.str());
            }
        }
    }

    BoundedCalibrationProblem calibrationProblem {};
    calibrationProblem.dataset = dataset;
    calibrationProblem.referenceCircuit = dcReference.circuit;
    calibrationProblem.initialParameters = report.parameters.isValid()
        ? report.parameters
        : ActiveModelParameters {};
    calibrationProblem.trainingDcScenarios = dataset.split.trainingDcScenarios;
    calibrationProblem.validationDcScenarios = dataset.split.validationDcScenarios;

    BoundedCalibrationOptions calibrationOptions {};
    calibrationOptions.transient = options.transient;

    BoundedCalibrationResult calibrationResult {};
    bool calibrationAttempted = false;
    if (options.enableBoundedCalibration) {
        if (!report.gates.dc) {
            report.calibrationFailures.push_back("calibration skipped: DC gate did not pass");
        } else if (!report.gates.ac) {
            report.calibrationFailures.push_back("calibration skipped: AC gate did not pass");
        } else {
            calibrationAttempted = true;
            const BoundedCalibrationSolver calibrationSolver {};
            calibrationResult = calibrationSolver.solve(calibrationProblem, calibrationOptions);
            report.parameters = calibrationResult.bestParameters;
            report.calibrationConverged = calibrationResult.converged
                && report.gates.dc
                && report.gates.ac
                && report.gates.transient;
            report.validationPassed = calibrationResult.validationPassed;
            report.calibrationTrainCost = calibrationResult.trainCost;
            report.calibrationValidationCost = calibrationResult.validationCost;
            report.calibrationIterations = calibrationResult.iterations;
            for (const auto& failure : calibrationResult.residualFailures) {
                report.calibrationFailures.push_back(failure);
            }
            for (const auto& failure : calibrationResult.calibrationFailures) {
                report.calibrationFailures.push_back(failure);
            }
            for (const auto& bound : calibrationResult.parametersOnBound) {
                report.notes.push_back("parameter on bound after calibration: " + bound);
                report.parametersOnBound.push_back(bound);
            }
            std::ostringstream summary {};
            summary << "calibration: trainCost=" << calibrationResult.trainCost
                    << ", validationCost=" << calibrationResult.validationCost
                    << ", iters=" << calibrationResult.iterations
                    << ", converged=" << (report.calibrationConverged ? "true" : "false");
            report.notes.push_back(summary.str());
            if (calibrationResult.converged && !report.calibrationConverged) {
                report.calibrationFailures.push_back("calibration convergence withheld until DC+AC+transient gates pass");
            }
        }
    } else {
        report.notes.push_back("bounded calibration disabled by OfflineCalibrationRunOptions");
    }

    if (options.enableIdentifiability && calibrationAttempted && report.calibrationConverged) {
        const IdentifiabilityAnalyzer analyzer {};
        const auto identifiability = analyzer.analyze(calibrationProblem,
                                                      calibrationResult.bestParameters,
                                                      calibrationOptions);
        report.gates.identifiability = identifiability.passed;
        report.identifiabilityConditionNumber = identifiability.conditionNumber;
        for (const auto& failure : identifiability.failures) {
            report.identifiabilityFailures.push_back(failure);
        }
        for (const auto& parameter : identifiability.weakParameters) {
            report.weakParameters.push_back(parameter);
            report.nonIdentifiableParameters.push_back(parameter);
        }
        for (const auto& correlation : identifiability.strongCorrelations) {
            const auto label = correlation.first + "~" + correlation.second;
            report.strongParameterCorrelations.push_back(label);
            report.nonIdentifiableParameters.push_back(label);
        }

        std::ostringstream summary {};
        summary << "identifiability: condition=" << identifiability.conditionNumber
                << ", weak=" << identifiability.weakParameters.size()
                << ", correlations=" << identifiability.strongCorrelations.size()
                << ", onBound=" << identifiability.parametersOnBound.size();
        report.notes.push_back(summary.str());
    } else if (options.enableIdentifiability) {
        report.identifiabilityFailures.push_back("identifiability skipped: calibration did not converge");
        report.gates.identifiability = false;
    } else {
        report.notes.push_back("identifiability analysis disabled by OfflineCalibrationRunOptions");
        report.gates.identifiability = false;
    }

    // Audio gate: when enabled, prove fidelity offline with the ThdBench.
    // Without the flag the gate remains strictly closed; promotion stays
    // blocked even when calibration and identifiability succeed. This keeps
    // backward compatibility with every caller that does not opt in.
    report.gates.audio = false;
    if (options.enableAudioGate) {
        const ThdBench thdBench {};
        report.thdBench = thdBench.evaluate(report.parameters, dataset, dcReference.circuit);

        std::ostringstream thdSummary {};
        thdSummary << "thd: measured=" << formatDbForNote(report.thdBench.measuredThdDb)
                   << ", targetCeiling=" << formatDbForNote(report.thdBench.goldenThdDb)
                   << ", tolerance=" << formatDbForNote(report.thdBench.toleranceDb)
                   << ", targetStatus=" << report.thdBench.targetStatus
                   << ", targetId=" << report.thdBench.targetId
                   << ", measurementNode=" << report.thdBench.measurementNode
                   << ", mode=" << report.thdBench.mode
                   << ", passed=" << (report.thdBench.passed ? "true" : "false");
        report.notes.push_back(thdSummary.str());
        for (const auto& failure : report.thdBench.failures) {
            report.notes.push_back("thd bench: " + failure);
        }

        const auto audioPasses = report.calibrationConverged
            && report.validationPassed
            && report.gates.identifiability
            && report.thdBench.passed
            && report.parametersOnBound.empty();
        report.gates.audio = audioPasses;
        if (!audioPasses) {
            report.notes.push_back("audio gate strict: requires calibrationConverged && validationPassed && identifiability && thdBench.passed && no parameter on bound");
        }
    } else {
        report.notes.push_back("audio gate disabled by OfflineCalibrationRunOptions; remains closed");
        report.notes.push_back("audio gate strict: requires realtime plugin THD measurement");
    }
    report.notes.push_back("boundary remains FULL_ACTIVE_MODEL_UNVERIFIED; promotion requires DC, AC, transient, audio and identifiability gates");
    return report;
}

} // namespace u273::reference::calibration
