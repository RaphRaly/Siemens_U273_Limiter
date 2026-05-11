#include "u273/reference/calibration/B6B11CalibrationRunner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "u273/reference/calibration/ActiveTopologyEvaluator.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/TransientReferenceSolver.h"
#include "u273/reference/state_space/U273NetlistLoader.h"

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
                                      std::string_view nodeName)
{
    const auto node = circuit.findNode(nodeName);
    if (node.value <= 0 || node.value >= circuit.nodeCount()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto index = static_cast<std::size_t>(node.value - 1);
    if (index >= state.unknowns.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return state.unknowns[index];
}

[[nodiscard]] double firstTimeAtFraction(const std::vector<TransientGoldenRow>& rows,
                                         double maxValue,
                                         double fraction)
{
    const auto threshold = maxValue * fraction;
    for (const auto& row : rows) {
        if (row.driveVolt >= threshold) {
            return row.timeSeconds;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

[[nodiscard]] std::vector<TransientModelSummary> buildQuasiStaticTransientSummaries(
    const CalibrationDataset& dataset,
    const ss::CircuitGraph& dcReferenceCircuit,
    const OperatingPointSolver& opSolver)
{
    std::vector<TransientModelSummary> summaries {};
    const auto sourceId = firstVoltageSourceId(dcReferenceCircuit);
    if (sourceId.empty()) {
        return summaries;
    }

    summaries.reserve(dataset.transientSummaries.size());
    for (const auto& golden : dataset.transientSummaries) {
        const auto rows = dataset.transientRowsForCase(golden.caseName);
        TransientModelSummary summary {};
        summary.caseName = golden.caseName;
        if (rows.empty()) {
            summaries.push_back(std::move(summary));
            continue;
        }

        auto maxDrive = 0.0;
        for (const auto& row : rows) {
            maxDrive = std::max(maxDrive, row.driveVolt);
        }

        summary.hasMaxDriveVolt = true;
        summary.maxDriveVolt = maxDrive;

        const auto peakCircuit = cloneWithCommandSourceVoltage(dcReferenceCircuit, sourceId, maxDrive);
        const auto peakView = DCCircuitView::fromCircuit(peakCircuit);
        const auto peakOp = opSolver.solve(peakView);
        const auto peakCmd = peakOp.converged
            ? nodeVoltageOrNan(peakView.circuit, peakOp.state, "CMD")
            : std::numeric_limits<double>::quiet_NaN();
        if (std::isfinite(peakCmd)) {
            summary.hasMaxCmdVolt = true;
            summary.maxCmdVolt = peakCmd;
        }

        const auto attack = firstTimeAtFraction(rows, maxDrive, 0.9);
        if (std::isfinite(attack)) {
            summary.hasAttack90TimeSeconds = true;
            summary.attack90TimeSeconds = attack;
        }

        summaries.push_back(std::move(summary));
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

    AcLinearizationResult ac {};
    const auto sourceId = firstVoltageSourceId(dcReference.circuit);
    const auto output = dcReference.circuit.findNode("CMD");
    if (!sourceId.empty() && output.value > 0) {
        AcSourcePort source {};
        source.voltageSourceId = sourceId;
        source.outputNode = output;
        source.amplitudeVolt = 1.0;
        const LinearizedAcSolver acSolver {};
        ac = acSolver.solveSmallSignal(dcReference.circuit, op, source, acFrequenciesOrFallback(dataset));
    } else {
        ac.failures.push_back("no usable DC execution AC source/output found in netlist");
    }

    AcResidualOptions acOptions {};
    acOptions.scenario = "rsource_10k";
    acOptions.driveVolt = 1.0;
    acOptions.matchDriveVolt = true;
    acOptions.commandSourceOhm = 10000.0;
    acOptions.matchCommandSourceOhm = true;
    const auto acGate = evaluateAcResiduals(dataset, ac, acOptions);
    report.gates.ac = acGate.passed;
    report.gates.referenceAc = acGate.passed;
    appendGateNotes(report, acGate);
    for (const auto& failure : ac.failures) {
        report.notes.push_back("AC solver: " + failure);
    }

    ss::StateSpaceSolver transientStateFactory {dcReference.circuit};
    {
        const TransientReferenceSolver transient {};
        const auto transientRun = transient.run(
            dcReference.circuit,
            transientStateFactory.createInitialState(),
            options.transient,
            options.transientSteps);
        report.notes.push_back("transient wrapper diagnostics: steps="
                               + std::to_string(transientRun.steps)
                               + ", solverSteps="
                               + std::to_string(transientRun.totalSolverSteps)
                               + ", retries="
                               + std::to_string(transientRun.retryCount)
                               + ", maxResidual="
                               + std::to_string(transientRun.maxResidualNorm));
        for (const auto& failure : transientRun.failures) {
            report.notes.push_back("transient wrapper: " + failure);
        }
    }

    const auto transientSummaries = buildQuasiStaticTransientSummaries(dataset, dcReference.circuit, opSolver);
    const auto transientGate = evaluateTransientResiduals(dataset, transientSummaries);
    report.gates.transient = transientGate.passed;
    report.gates.referenceTransient = transientGate.passed;
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

    report.gates.audio = false;
    report.gates.identifiability = false;
    report.notes.push_back("audio and identifiability gates are intentionally open until offline residuals pass");
    report.notes.push_back("boundary remains FULL_ACTIVE_MODEL_UNVERIFIED; promotion requires DC, AC, transient, audio and identifiability gates");
    return report;
}

} // namespace u273::reference::calibration
