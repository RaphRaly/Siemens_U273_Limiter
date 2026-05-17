// u273_ac_probe - diagnostic CLI for the AC residual gate.
//
// Reads the same golden dataset and dcExecutionReference netlist as the
// offline runner, solves the DC operating point and the linearized AC
// transfer function on the dataset frequencies, then writes a per-frequency
// CSV (golden vs model magnitude/phase, deltas, per-tolerance pass flag)
// and an AC_DIAGNOSTIC.md summary that breaks down failures by decade.
//
// Goal: localize WHICH frequency band drives the AC gate failure, so the
// next sprint step (LM calibration or topology fix) is targeted.
//
// Exit codes: 0 on success (even when the AC gate fails), 1 only on
// malformed argv.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/state_space/CircuitGraph.h"
#include "u273/reference/state_space/U273NetlistLoader.h"
#include "u273/reference/state_space/U273ReferenceCircuitBuilder.h"

namespace fs = std::filesystem;
namespace calib = u273::reference::calibration;
namespace ss = u273::reference::state_space;

namespace {

#ifndef U273_DEFAULT_DATASET_DIR
#define U273_DEFAULT_DATASET_DIR "results"
#endif

constexpr double kMagnitudeToleranceDb = 0.5;
constexpr double kPhaseToleranceRad = 0.08726646259971647; // 5 deg

[[nodiscard]] std::string timestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream s;
    s << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return s.str();
}

[[nodiscard]] std::string firstB11SourceId(const ss::CircuitGraph& circuit)
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id.find("B11") != std::string::npos) {
            return source.id;
        }
    }
    return circuit.voltageSources().empty() ? std::string {} : circuit.voltageSources().front().id;
}

[[nodiscard]] const calib::AcLinearizationPoint* nearestModelPoint(
    const std::vector<calib::AcLinearizationPoint>& points,
    double frequencyHz)
{
    const calib::AcLinearizationPoint* best = nullptr;
    double bestDelta = std::numeric_limits<double>::infinity();
    for (const auto& point : points) {
        const auto delta = std::abs(point.frequencyHz - frequencyHz);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = &point;
        }
    }
    if (best == nullptr) {
        return nullptr;
    }
    const auto reference = std::max(std::abs(frequencyHz), 1.0);
    if (bestDelta / reference > 1.0e-3) {
        return nullptr;
    }
    return best;
}

[[nodiscard]] std::string formatDouble(double value, int precision)
{
    if (!std::isfinite(value)) {
        return "n/a";
    }
    std::ostringstream s;
    s << std::fixed << std::setprecision(precision) << value;
    return s.str();
}

[[nodiscard]] std::string boolStr(bool value)
{
    return value ? "true" : "false";
}

struct DecadeBucket {
    std::string label {};
    double lowHz {};
    double highHz {};
    int total {};
    int magFailed {};
    int phaseFailed {};
    double worstMagDeltaDb {};
    double worstPhaseDeltaRad {};
    double worstMagFrequencyHz {};
    double worstPhaseFrequencyHz {};
};

struct ScenarioTargetBucket {
    std::string scenario {};
    std::string target {};
    int total {};
    int magFailed {};
    int phaseFailed {};
    double worstMagDeltaDb {};
    double worstPhaseDeltaRad {};
    double worstMagFrequencyHz {};
    double worstPhaseFrequencyHz {};
};

struct AcTargetRun {
    std::string target {};
    std::string nodeName {};
};

struct ModelPointResult {
    bool solved {};
    calib::AcLinearizationPoint point {};
    std::vector<std::string> failures {};
};

struct CliOptions {
    fs::path outputDir {};
    fs::path datasetDir {};
    bool ok {true};
};

[[nodiscard]] CliOptions parseCli(int argc, char** argv)
{
    CliOptions o {};
    o.datasetDir = fs::path {U273_DEFAULT_DATASET_DIR};
    o.outputDir = fs::path {"Demo"} / ("ac_probe_" + timestampNow());
    for (int i = 1; i < argc; ++i) {
        const std::string arg {argv[i]};
        if (arg == "--help" || arg == "-h") {
            std::cout << "u273_ac_probe [--output-dir DIR] [--dataset-dir DIR]\n";
            o.ok = false;
            return o;
        }
        if ((arg == "--output-dir" || arg == "--dataset-dir") && (i + 1 >= argc)) {
            std::cerr << "u273_ac_probe: " << arg << " requires a value\n";
            o.ok = false;
            return o;
        }
        if (arg == "--output-dir") {
            o.outputDir = fs::path {argv[++i]};
        } else if (arg == "--dataset-dir") {
            o.datasetDir = fs::path {argv[++i]};
        } else {
            std::cerr << "u273_ac_probe: unknown argument '" << arg << "'\n";
            o.ok = false;
            return o;
        }
    }
    return o;
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

[[nodiscard]] bool resistorConnects(const ss::Resistor& resistor, ss::NodeId a, ss::NodeId b) noexcept
{
    return (resistor.positive == a && resistor.negative == b)
        || (resistor.positive == b && resistor.negative == a);
}

[[nodiscard]] ss::CircuitGraph cloneWithCommandSource(const ss::CircuitGraph& source,
                                                      double commandSourceVolt,
                                                      double commandSourceOhm)
{
    ss::CircuitGraph target {};
    copyNodes(source, target);

    const auto drive = source.findNode("B11_DRV");
    const auto command = source.findNode("CMD");

    for (const auto& resistor : source.resistors()) {
        const auto replacement = resistor.id == "RB11_S6_S7_CMD"
            || (drive.value > 0 && command.value > 0 && resistorConnects(resistor, drive, command))
            ? commandSourceOhm
            : resistor.resistanceOhm;
        target.addResistor(resistor.id,
                           sameNode(source, target, resistor.positive),
                           sameNode(source, target, resistor.negative),
                           replacement);
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
    for (const auto& voltageSource : source.voltageSources()) {
        const auto sourceDrivesB11 = drive.value > 0
            && voltageSource.positive == drive
            && voltageSource.negative == ss::kGroundNode;
        const auto replacement = sourceDrivesB11 || voltageSource.id.find("B11") != std::string::npos
            ? commandSourceVolt
            : voltageSource.voltage;
        target.addVoltageSource(voltageSource.id,
                                sameNode(source, target, voltageSource.positive),
                                sameNode(source, target, voltageSource.negative),
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
        if (std::isfinite(anodeVolt) && std::isfinite(cathodeVolt)) {
            setBridgeDiodeConductance(options,
                                      diode.id,
                                      diode.model.conductanceSiemens(anodeVolt - cathodeVolt));
        }
    }

    return options;
}

[[nodiscard]] calib::OperatingPointResult makeSolvedLinearOperatingPoint(const ss::CircuitGraph& circuit)
{
    const ss::StateSpaceSolver stateFactory {circuit};
    calib::OperatingPointResult op {};
    op.validInput = true;
    op.converged = true;
    op.state = stateFactory.createInitialState();
    return op;
}

[[nodiscard]] ModelPointResult solveModelPoint(const ss::CircuitGraph& dcTemplate,
                                               const calib::AcGoldenPoint& golden,
                                               const std::string& nodeName)
{
    ModelPointResult result {};
    const auto dcCircuit = cloneWithCommandSource(dcTemplate,
                                                  golden.driveVolt,
                                                  golden.commandSourceOhm);
    const calib::OperatingPointSolver opSolver {};
    const auto op = opSolver.solve(calib::DCCircuitView::fromCircuit(dcCircuit));
    if (!op.converged) {
        result.failures.push_back("DC OP did not converge");
        for (const auto& failure : op.failures) {
            result.failures.push_back("OP: " + failure);
        }
        return result;
    }

    const auto acOptions = buildB6AcOptionsFromOperatingPoint(dcCircuit,
                                                             op.state,
                                                             golden.commandSourceOhm);
    const auto acCircuit = ss::U273ReferenceCircuitBuilder::buildB6BridgeSmallSignalAcReference(acOptions);
    const auto output = acCircuit.findNode(nodeName);
    if (output.value <= 0) {
        result.failures.push_back("AC output node not found: " + nodeName);
        return result;
    }

    calib::AcSourcePort source {};
    source.voltageSourceId = "VAC";
    source.outputNode = output;
    source.amplitudeVolt = 1.0;

    const calib::LinearizedAcSolver acSolver {};
    const auto ac = acSolver.solveSmallSignal(acCircuit,
                                              makeSolvedLinearOperatingPoint(acCircuit),
                                              source,
                                              std::vector<double> {golden.frequencyHz});
    if (!ac.solved || ac.points.empty()) {
        result.failures.push_back("AC linearization did not solve");
        for (const auto& failure : ac.failures) {
            result.failures.push_back("AC: " + failure);
        }
        return result;
    }

    result.solved = true;
    result.point = ac.points.front();
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    const auto cli = parseCli(argc, argv);
    if (!cli.ok) {
        return 1;
    }

    std::error_code ec;
    fs::create_directories(cli.outputDir, ec);
    if (ec) {
        std::cerr << "u273_ac_probe: could not create output directory '"
                  << cli.outputDir.string() << "': " << ec.message() << "\n";
        return 0;
    }

    std::cout << "u273_ac_probe: dataset-dir = " << cli.datasetDir.string() << "\n";
    std::cout << "u273_ac_probe: output-dir  = " << cli.outputDir.string() << "\n";

    std::vector<std::string> probeFailures {};
    std::vector<std::string> probeNotes {};

    const auto dataset = calib::CalibrationDataset::loadFromResultsDirectory(cli.datasetDir);
    if (!dataset.isValid()) {
        probeFailures.push_back("dataset is not valid");
        for (const auto& failure : dataset.failures) {
            probeFailures.push_back("dataset: " + failure);
        }
    }

    const auto netlistPath = cli.datasetDir / "u273_netlist.json";
    ss::U273NetlistLoaderOptions loaderOptions {};
    loaderOptions.source = ss::U273NetlistSource::dcExecutionReference;
    const auto loaded = ss::U273NetlistLoader::loadFromFile(netlistPath, loaderOptions);
    if (!loaded.report.hasUsableCircuit()) {
        probeFailures.push_back("dcExecutionReference netlist loader did not produce a usable circuit");
    }

    bool dcConverged = false;
    calib::OperatingPointResult op {};
    calib::DCCircuitView dcView {};
    if (loaded.report.hasUsableCircuit()) {
        dcView = calib::DCCircuitView::fromCircuit(loaded.circuit);
        const calib::OperatingPointSolver opSolver {};
        op = opSolver.solve(dcView);
        dcConverged = op.converged;
        if (!dcConverged) {
            probeFailures.push_back("DC operating point did not converge");
            for (const auto& failure : op.failures) {
                probeFailures.push_back("OP: " + failure);
            }
        }
    }

    std::array<AcTargetRun, 2> acTargets {{
        {"VCMD", "CMD"},
        {"VNB", "NB"}
    }};
    probeNotes.push_back("AC model: per-row B6 small-signal reference with finite command source impedance");
    probeNotes.push_back("AC source id: VAC");

    // Compose CSV: per-frequency, per-target (CMD/NB) comparison.
    const auto csvPath = cli.outputDir / "ac_residuals.csv";
    std::ofstream csv {csvPath};
    if (!csv) {
        probeFailures.push_back("could not open ac_residuals.csv for writing");
    } else {
        csv << "scenario,driveVolt,commandSourceOhm,target,frequencyHz,goldenMagDb,modelMagDb,deltaMagDb,"
            << "goldenPhaseRad,modelPhaseRad,deltaPhaseRad,"
            << "magPassed,phasePassed\n";
        csv << std::scientific << std::setprecision(9);
    }

    const auto wrapPhaseDelta = [](double delta) {
        const auto kPi = 3.14159265358979323846;
        while (delta > kPi) delta -= 2.0 * kPi;
        while (delta < -kPi) delta += 2.0 * kPi;
        return delta;
    };

    std::array<DecadeBucket, 5> buckets {{
        {"<10 Hz",     0.0,     10.0,     0, 0, 0, 0.0, 0.0, 0.0, 0.0},
        {"10-100 Hz",  10.0,    100.0,    0, 0, 0, 0.0, 0.0, 0.0, 0.0},
        {"100 Hz-1 kHz", 100.0, 1000.0,   0, 0, 0, 0.0, 0.0, 0.0, 0.0},
        {"1-10 kHz",   1000.0,  10000.0,  0, 0, 0, 0.0, 0.0, 0.0, 0.0},
        {">=10 kHz",   10000.0, 1.0e12,   0, 0, 0, 0.0, 0.0, 0.0, 0.0}
    }};

    int totalPoints = 0;
    int totalMagFail = 0;
    int totalPhaseFail = 0;
    int strictTotal = 0;
    int strictMagFail = 0;
    int strictPhaseFail = 0;
    std::vector<ScenarioTargetBucket> scenarioTargetBuckets {};

    if (csv && !dataset.acPoints.empty()) {
        for (const auto& golden : dataset.acPoints) {
            // Find the bucket once per golden frequency.
            DecadeBucket* bucket = nullptr;
            for (auto& candidate : buckets) {
                if (golden.frequencyHz >= candidate.lowHz && golden.frequencyHz < candidate.highHz) {
                    bucket = &candidate;
                    break;
                }
            }

            auto scenarioTargetBucket = [&](const std::string& target) -> ScenarioTargetBucket& {
                const auto found = std::find_if(scenarioTargetBuckets.begin(),
                                                scenarioTargetBuckets.end(),
                                                [&](const ScenarioTargetBucket& candidate) {
                    return candidate.scenario == golden.scenario && candidate.target == target;
                });
                if (found != scenarioTargetBuckets.end()) {
                    return *found;
                }

                ScenarioTargetBucket created {};
                created.scenario = golden.scenario;
                created.target = target;
                scenarioTargetBuckets.push_back(std::move(created));
                return scenarioTargetBuckets.back();
            };

            auto modelForTarget = [&](const std::string& target) -> ModelPointResult {
                for (const auto& run : acTargets) {
                    if (run.target == target) {
                        return solveModelPoint(loaded.circuit, golden, run.nodeName);
                    }
                }
                ModelPointResult missing {};
                missing.failures.push_back("unknown AC target: " + target);
                return missing;
            };

            auto emitRow = [&](const std::string& target,
                               double goldenMagDb,
                               double goldenPhaseRad) {
                const auto model = modelForTarget(target);
                if (!model.solved) {
                    const auto context = " scenario=" + golden.scenario
                        + " drive=" + std::to_string(golden.driveVolt)
                        + " sourceOhm=" + std::to_string(golden.commandSourceOhm)
                        + " frequency=" + std::to_string(golden.frequencyHz);
                    probeFailures.push_back("missing AC model point for " + target + context);
                    for (const auto& failure : model.failures) {
                        probeFailures.push_back("AC " + target + context + ": " + failure);
                    }
                    return;
                }
                const auto deltaMagDb = model.point.magnitudeDb - goldenMagDb;
                const auto deltaPhaseRad = wrapPhaseDelta(model.point.phaseRadians - goldenPhaseRad);
                const auto magPassed = std::abs(deltaMagDb) <= kMagnitudeToleranceDb;
                const auto phasePassed = std::abs(deltaPhaseRad) <= kPhaseToleranceRad;
                csv << golden.scenario << ","
                    << golden.driveVolt << ","
                    << golden.commandSourceOhm << ","
                    << target << ","
                    << golden.frequencyHz << ","
                    << goldenMagDb << ","
                    << model.point.magnitudeDb << ","
                    << deltaMagDb << ","
                    << goldenPhaseRad << ","
                    << model.point.phaseRadians << ","
                    << deltaPhaseRad << ","
                    << (magPassed ? "true" : "false") << ","
                    << (phasePassed ? "true" : "false") << "\n";

                ++totalPoints;
                if (!magPassed) ++totalMagFail;
                if (!phasePassed) ++totalPhaseFail;
                const auto inStrictSlice = golden.scenario == "rsource_10k"
                    && target == "VCMD"
                    && std::abs(golden.driveVolt - 1.0) < 1.0e-9
                    && std::abs(golden.commandSourceOhm - 10000.0) < 1.0e-6;
                if (inStrictSlice) {
                    ++strictTotal;
                    if (!magPassed) ++strictMagFail;
                    if (!phasePassed) ++strictPhaseFail;
                }

                auto& st = scenarioTargetBucket(target);
                ++st.total;
                if (!magPassed) {
                    ++st.magFailed;
                    if (std::abs(deltaMagDb) > std::abs(st.worstMagDeltaDb)) {
                        st.worstMagDeltaDb = deltaMagDb;
                        st.worstMagFrequencyHz = golden.frequencyHz;
                    }
                }
                if (!phasePassed) {
                    ++st.phaseFailed;
                    if (std::abs(deltaPhaseRad) > std::abs(st.worstPhaseDeltaRad)) {
                        st.worstPhaseDeltaRad = deltaPhaseRad;
                        st.worstPhaseFrequencyHz = golden.frequencyHz;
                    }
                }

                if (bucket != nullptr) {
                    ++bucket->total;
                    if (!magPassed) {
                        ++bucket->magFailed;
                        if (std::abs(deltaMagDb) > std::abs(bucket->worstMagDeltaDb)) {
                            bucket->worstMagDeltaDb = deltaMagDb;
                            bucket->worstMagFrequencyHz = golden.frequencyHz;
                        }
                    }
                    if (!phasePassed) {
                        ++bucket->phaseFailed;
                        if (std::abs(deltaPhaseRad) > std::abs(bucket->worstPhaseDeltaRad)) {
                            bucket->worstPhaseDeltaRad = deltaPhaseRad;
                            bucket->worstPhaseFrequencyHz = golden.frequencyHz;
                        }
                    }
                }
            };

            if (golden.hasCmd) {
                emitRow("VCMD", golden.cmdMagnitudeDb, golden.cmdPhaseRadians);
            }
            if (golden.hasNb) {
                emitRow("VNB", golden.nbMagnitudeDb, golden.nbPhaseRadians);
            }
        }
    }

    if (csv) {
        csv.close();
    }

    // Markdown summary.
    const auto mdPath = cli.outputDir / "AC_DIAGNOSTIC.md";
    std::ofstream md {mdPath};
    if (md) {
        const auto stamp = timestampNow();
        md << "# U273 AC Diagnostic " << stamp << "\n\n";
        md << "## Inputs\n";
        md << "- dataset: `" << cli.datasetDir.string() << "`\n";
        md << "- AC frequencies in dataset: " << dataset.acFrequenciesHz.size() << "\n";
        md << "- AC golden points: " << dataset.acPoints.size() << "\n";
        md << "- magnitudeToleranceDb: " << kMagnitudeToleranceDb << " dB\n";
        md << "- phaseToleranceRad: " << kPhaseToleranceRad
           << " rad (~" << (kPhaseToleranceRad * 180.0 / 3.14159265358979323846) << " deg)\n\n";

        md << "## DC operating point\n";
        md << "- converged: " << boolStr(dcConverged) << "\n";
        md << "- attempts: " << op.attempts << "\n";
        for (const auto& log : op.attemptLog) {
            md << "- attempt strategy=" << log.strategy
               << " iters=" << log.iterations
               << " residual=" << log.residualNorm
               << " converged=" << boolStr(log.converged)
               << " continuationSteps=" << log.continuationSteps << "\n";
        }
        md << "\n";

        md << "## AC linearization\n";
        for (const auto& target : acTargets) {
            md << "- " << target.target
               << " node=`" << target.nodeName << "`"
               << " solved per golden row\n";
        }
        md << "- source: `VAC`\n";
        md << "- command port: finite per-row source impedance from golden `commandSourceOhm`\n";
        md << "\n";

        md << "## Per-decade failure counts\n";
        md << "| Decade | Total | Mag failed | Phase failed | Worst dMag (dB) @ Hz | Worst dPhase (rad) @ Hz |\n";
        md << "|---|---|---|---|---|---|\n";
        for (const auto& bucket : buckets) {
            md << "| " << bucket.label
               << " | " << bucket.total
               << " | " << bucket.magFailed
               << " | " << bucket.phaseFailed
               << " | " << formatDouble(bucket.worstMagDeltaDb, 3) << " @ " << formatDouble(bucket.worstMagFrequencyHz, 1)
               << " | " << formatDouble(bucket.worstPhaseDeltaRad, 4) << " @ " << formatDouble(bucket.worstPhaseFrequencyHz, 1)
               << " |\n";
        }
        md << "\n";

        md << "## Per-scenario / target failure counts\n";
        md << "| Scenario | Target | Total | Mag failed | Phase failed | Worst dMag (dB) @ Hz | Worst dPhase (rad) @ Hz |\n";
        md << "|---|---|---|---|---|---|---|\n";
        for (const auto& bucket : scenarioTargetBuckets) {
            md << "| " << bucket.scenario
               << " | " << bucket.target
               << " | " << bucket.total
               << " | " << bucket.magFailed
               << " | " << bucket.phaseFailed
               << " | " << formatDouble(bucket.worstMagDeltaDb, 3) << " @ " << formatDouble(bucket.worstMagFrequencyHz, 1)
               << " | " << formatDouble(bucket.worstPhaseDeltaRad, 4) << " @ " << formatDouble(bucket.worstPhaseFrequencyHz, 1)
               << " |\n";
        }
        md << "\n";

        const auto strictPass = strictTotal > 0 && strictMagFail == 0 && strictPhaseFail == 0;
        md << "## Strict Gate Slice\n";
        md << "- required slice: `rsource_10k / drive=1V / VCMD`\n";
        md << "- total rows: " << strictTotal << "\n";
        md << "- magnitude failed: " << strictMagFail << "\n";
        md << "- phase failed: " << strictPhaseFail << "\n";
        md << "- pass: " << boolStr(strictPass) << "\n\n";

        md << "## Diagnostic Hypotheses\n";
        md << "- B11 source port: inspect `AC source id` above and compare against the loaded netlist voltage sources.\n";
        md << "- Target node mapping: VCMD and VNB are solved as separate output nodes; failures isolated to one target imply node/mapping risk.\n";
        md << "- Amplitude normalization: model rows are normalized by the configured 1 V small-signal source.\n";
        md << "- Dynamic components: this probe uses a dedicated B6 AC reference circuit with C1/C2/C3/C5/C7 active.\n";
        md << "- Dataset equivalence: if the strict slice fails while source and nodes are correct, treat the golden dataset/topology equivalence as unresolved.\n\n";

        md << "## Totals\n";
        md << "- total comparison rows: " << totalPoints << "\n";
        md << "- magnitude failed: " << totalMagFail << "\n";
        md << "- phase failed: " << totalPhaseFail << "\n";
        if (totalPoints > 0) {
            const auto magFraction = 100.0 * totalMagFail / totalPoints;
            const auto phaseFraction = 100.0 * totalPhaseFail / totalPoints;
            md << "- mag fail rate: " << formatDouble(magFraction, 1) << " %\n";
            md << "- phase fail rate: " << formatDouble(phaseFraction, 1) << " %\n";
        }
        md << "\n";

        md << "## Notes\n";
        for (const auto& note : probeNotes) {
            md << "- " << note << "\n";
        }
        if (probeNotes.empty()) {
            md << "- (none)\n";
        }
        md << "\n";

        md << "## Failures\n";
        for (const auto& f : probeFailures) {
            md << "- " << f << "\n";
        }
        if (probeFailures.empty()) {
            md << "- (none)\n";
        }
    }

    std::cout << "u273_ac_probe: csv  = " << csvPath.string() << "\n";
    std::cout << "u273_ac_probe: md   = " << mdPath.string() << "\n";
    std::cout << "u273_ac_probe: total=" << totalPoints
              << " magFail=" << totalMagFail
              << " phaseFail=" << totalPhaseFail << "\n";
    return 0;
}
