#include "u273/reference/calibration/CalibrationResiduals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

[[nodiscard]] bool finite(double value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] double absOrInfinity(double value) noexcept
{
    return finite(value) ? std::abs(value) : std::numeric_limits<double>::infinity();
}

[[nodiscard]] double wrappedPhaseError(double model, double measured) noexcept
{
    return std::atan2(std::sin(model - measured), std::cos(model - measured));
}

[[nodiscard]] ResidualPoint makePoint(std::string name,
                                      std::string context,
                                      std::string unit,
                                      double measured,
                                      double model,
                                      double tolerance,
                                      bool isPhase = false)
{
    ResidualPoint point {};
    point.name = std::move(name);
    point.context = std::move(context);
    point.unit = std::move(unit);
    point.measured = measured;
    point.model = model;
    point.tolerance = tolerance;
    point.rawError = isPhase ? wrappedPhaseError(model, measured) : model - measured;
    point.weightedError = tolerance > 0.0
        ? absOrInfinity(point.rawError) / tolerance
        : std::numeric_limits<double>::infinity();
    point.status = point.weightedError <= 1.0 ? ResidualStatus::passed : ResidualStatus::failed;
    if (!finite(measured) || !finite(model) || !(tolerance > 0.0)) {
        point.status = ResidualStatus::failed;
    }
    return point;
}

[[nodiscard]] ResidualPoint makeSkippedPoint(std::string name,
                                             std::string context,
                                             std::string unit)
{
    ResidualPoint point {};
    point.name = std::move(name);
    point.context = std::move(context);
    point.unit = std::move(unit);
    point.status = ResidualStatus::skipped;
    return point;
}

void finishGate(ResidualGateResult& gate)
{
    gate.evaluated = !gate.sets.empty();
    gate.passed = gate.validInput && gate.evaluated && gate.failures.empty();
    gate.worstWeightedError = 0.0;

    for (const auto& set : gate.sets) {
        gate.passed = gate.passed && set.passed();
        for (const auto& point : set.points) {
            if (point.status != ResidualStatus::skipped && finite(point.weightedError)) {
                gate.worstWeightedError = std::max(gate.worstWeightedError, point.weightedError);
            }
        }
    }
}

[[nodiscard]] std::optional<double> nodeVoltage(const ss::CircuitGraph& circuit,
                                                const ss::CircuitState& state,
                                                std::string_view nodeName) noexcept
{
    const auto node = circuit.findNode(nodeName);
    if (node.value <= 0 || node.value >= circuit.nodeCount()) {
        return std::nullopt;
    }

    const auto index = static_cast<std::size_t>(node.value - 1);
    if (index >= state.unknowns.size()) {
        return std::nullopt;
    }
    return state.unknowns[index];
}

[[nodiscard]] std::optional<double> aliasedNodeVoltage(const ss::CircuitGraph& circuit,
                                                       const ss::CircuitState& state,
                                                       std::string_view canonical)
{
    if (const auto exact = nodeVoltage(circuit, state, canonical)) {
        return exact;
    }

    if (canonical == "CMD") {
        return nodeVoltage(circuit, state, "B6.CMD");
    }
    if (canonical == "NB") {
        return nodeVoltage(circuit, state, "B6.NB");
    }
    if (canonical == "NL") {
        return nodeVoltage(circuit, state, "B6.NA");
    }
    if (canonical == "B11_DRV") {
        return nodeVoltage(circuit, state, "B6.VS");
    }

    return std::nullopt;
}

struct CommandSourceSignature {
    bool hasVoltage {};
    double voltage {};
    bool hasResistance {};
    double resistanceOhm {};
};

[[nodiscard]] bool sameNode(ss::NodeId lhs, ss::NodeId rhs) noexcept
{
    return lhs.value == rhs.value && lhs.value > 0;
}

[[nodiscard]] CommandSourceSignature detectCommandSource(const ss::CircuitGraph& circuit)
{
    CommandSourceSignature signature {};
    const auto drive = circuit.findNode("B11_DRV");
    const auto cmd = circuit.findNode("CMD");

    for (const auto& source : circuit.voltageSources()) {
        const auto sourceDrivesB11 = sameNode(source.positive, drive)
            && source.negative == ss::kGroundNode;
        const auto sourceLooksLikeB11 = source.id.find("B11") != std::string::npos
            && source.negative == ss::kGroundNode;
        if (sourceDrivesB11 || sourceLooksLikeB11) {
            signature.hasVoltage = true;
            signature.voltage = source.voltage;
            break;
        }
    }

    if (drive.value > 0 && cmd.value > 0) {
        for (const auto& resistor : circuit.resistors()) {
            const auto forward = resistor.positive == drive && resistor.negative == cmd;
            const auto reverse = resistor.positive == cmd && resistor.negative == drive;
            if ((forward || reverse) && resistor.resistanceOhm > 0.0) {
                signature.hasResistance = true;
                signature.resistanceOhm = resistor.resistanceOhm;
                break;
            }
        }
    }

    return signature;
}

[[nodiscard]] const DcGoldenScenario* findMatchingDcScenario(const CalibrationDataset& dataset,
                                                             const ss::CircuitGraph& circuit)
{
    const auto signature = detectCommandSource(circuit);
    if (signature.hasVoltage && signature.hasResistance) {
        for (const auto& scenario : dataset.dcScenarios) {
            if (std::abs(scenario.commandSourceVolt - signature.voltage) < 1.0e-9
                && std::abs(scenario.commandSourceOhm - signature.resistanceOhm) < 1.0e-6) {
                return &scenario;
            }
        }
    }
    return nullptr;
}

void addDcPoint(ResidualSet& set,
                const ss::CircuitGraph& circuit,
                const ss::CircuitState& state,
                std::string_view node,
                double measured,
                double tolerance,
                bool required)
{
    if (const auto model = aliasedNodeVoltage(circuit, state, node)) {
        set.points.push_back(makePoint(std::string {node},
                                       "DC operating point",
                                       "V",
                                       measured,
                                       *model,
                                       tolerance));
        return;
    }

    set.points.push_back(makeSkippedPoint(std::string {node}, "DC operating point", "V"));
    if (required) {
        set.failures.push_back(std::string {"missing model node for required DC residual: "} + std::string {node});
    } else {
        set.notes.push_back(std::string {"optional DC residual skipped because model node is absent: "}
                            + std::string {node});
    }
}

[[nodiscard]] bool pointMatchesOptions(const AcGoldenPoint& point, const AcResidualOptions& options)
{
    if (!options.scenario.empty() && point.scenario != options.scenario) {
        return false;
    }
    if (options.matchDriveVolt && std::abs(point.driveVolt - options.driveVolt) > 1.0e-9) {
        return false;
    }
    if (options.matchCommandSourceOhm
        && std::abs(point.commandSourceOhm - options.commandSourceOhm) > 1.0e-6) {
        return false;
    }
    return true;
}

[[nodiscard]] const AcGoldenPoint* findGoldenAcPoint(const std::vector<AcGoldenPoint>& goldenPoints,
                                                     double frequencyHz,
                                                     const AcResidualOptions& options)
{
    for (const auto& point : goldenPoints) {
        if (std::abs(point.frequencyHz - frequencyHz) < 1.0e-9 && pointMatchesOptions(point, options)) {
            return &point;
        }
    }
    return nullptr;
}

[[nodiscard]] bool selectAcTarget(const AcGoldenPoint& point,
                                  const AcResidualOptions& options,
                                  double& magnitudeDb,
                                  double& phaseRadians)
{
    if (options.target == "VNB") {
        if (!point.hasNb) {
            return false;
        }
        magnitudeDb = point.nbMagnitudeDb;
        phaseRadians = point.nbPhaseRadians;
        return true;
    }

    if (!point.hasCmd) {
        return false;
    }
    magnitudeDb = point.cmdMagnitudeDb;
    phaseRadians = point.cmdPhaseRadians;
    return true;
}

[[nodiscard]] const TransientModelSummary* findModelSummary(
    const std::vector<TransientModelSummary>& summaries,
    const std::string& caseName)
{
    for (const auto& summary : summaries) {
        if (summary.caseName == caseName) {
            return &summary;
        }
    }
    return nullptr;
}

void addTransientPoint(ResidualSet& set,
                       std::string name,
                       double measured,
                       bool modelAvailable,
                       double model,
                       double tolerance,
                       std::string unit,
                       bool required)
{
    if (!modelAvailable) {
        set.points.push_back(makeSkippedPoint(name, set.name, unit));
        set.notes.push_back(name + " unavailable in transient model summary");
        if (required) {
            set.failures.push_back(name + " is required but unavailable in transient model summary");
        }
        return;
    }

    set.points.push_back(makePoint(name, set.name, unit, measured, model, tolerance));
}

} // namespace

bool ResidualSet::passed() const noexcept
{
    if (!validInput || !evaluated || !failures.empty()) {
        return false;
    }

    auto compared = false;
    for (const auto& point : points) {
        if (point.failed()) {
            return false;
        }
        compared = compared || point.status == ResidualStatus::passed;
    }
    return compared;
}

ResidualGateResult evaluateDcResiduals(const CalibrationDataset& dataset,
                                       const ss::CircuitGraph& circuit,
                                       const OperatingPointResult& operatingPoint,
                                       const DcResidualOptions& options)
{
    ResidualGateResult gate {};
    gate.gateName = "DC residuals";
    gate.validInput = dataset.isValid()
        && operatingPoint.converged
        && operatingPoint.state.unknowns.size() == static_cast<std::size_t>(circuit.unknownCount())
        && options.voltageToleranceVolt > 0.0
        && options.sourceVoltageToleranceVolt > 0.0;

    if (!gate.validInput) {
        gate.failures.push_back("DC residuals require a valid dataset, converged OP and positive tolerances");
        finishGate(gate);
        return gate;
    }

    const auto* scenario = findMatchingDcScenario(dataset, circuit);
    if (scenario == nullptr) {
        gate.failures.push_back("no DC golden scenario matches the circuit command source");
        finishGate(gate);
        return gate;
    }

    ResidualSet set {};
    set.name = scenario->name;
    set.validInput = true;
    set.evaluated = true;
    addDcPoint(set, circuit, operatingPoint.state, "CMD", scenario->cmdVolt, options.voltageToleranceVolt, true);
    addDcPoint(set, circuit, operatingPoint.state, "NB", scenario->nbVolt, options.voltageToleranceVolt, true);
    if (scenario->hasNlVolt) {
        addDcPoint(set, circuit, operatingPoint.state, "NL", scenario->nlVolt, options.voltageToleranceVolt, false);
    }
    if (scenario->hasNrVolt) {
        addDcPoint(set, circuit, operatingPoint.state, "NR", scenario->nrVolt, options.voltageToleranceVolt, false);
    }
    if (scenario->hasB11DriveVolt) {
        addDcPoint(set,
                   circuit,
                   operatingPoint.state,
                   "B11_DRV",
                   scenario->b11DriveVolt,
                   options.sourceVoltageToleranceVolt,
                   false);
    }

    gate.sets.push_back(std::move(set));
    finishGate(gate);
    return gate;
}

ResidualGateResult evaluateAcResiduals(const CalibrationDataset& dataset,
                                       const AcLinearizationResult& ac,
                                       const AcResidualOptions& options)
{
    return evaluateAcResiduals(dataset.acPoints, ac, options);
}

ResidualGateResult evaluateAcResiduals(const std::vector<AcGoldenPoint>& goldenPoints,
                                       const AcLinearizationResult& ac,
                                       const AcResidualOptions& options)
{
    ResidualGateResult gate {};
    gate.gateName = "AC residuals";
    gate.validInput = ac.solved
        && !ac.points.empty()
        && !goldenPoints.empty()
        && options.magnitudeToleranceDb > 0.0
        && options.phaseToleranceRadians > 0.0;

    if (!gate.validInput) {
        gate.failures.push_back("AC residuals require solved model points, golden points and positive tolerances");
        finishGate(gate);
        return gate;
    }

    ResidualSet set {};
    set.name = options.target;
    set.validInput = true;
    set.evaluated = true;

    for (const auto& modelPoint : ac.points) {
        const auto* golden = findGoldenAcPoint(goldenPoints, modelPoint.frequencyHz, options);
        if (golden == nullptr) {
            set.failures.push_back("missing AC golden point for frequency " + std::to_string(modelPoint.frequencyHz));
            continue;
        }

        double goldenMagnitudeDb {};
        double goldenPhaseRadians {};
        if (!selectAcTarget(*golden, options, goldenMagnitudeDb, goldenPhaseRadians)) {
            set.failures.push_back("AC golden point does not expose target " + options.target);
            continue;
        }

        const auto context = golden->scenario + " @ " + std::to_string(modelPoint.frequencyHz) + " Hz";
        set.points.push_back(makePoint("magnitude", context, "dB", goldenMagnitudeDb, modelPoint.magnitudeDb,
                                       options.magnitudeToleranceDb));
        set.points.push_back(makePoint("phase", context, "rad", goldenPhaseRadians, modelPoint.phaseRadians,
                                       options.phaseToleranceRadians, true));
    }

    gate.sets.push_back(std::move(set));
    finishGate(gate);
    return gate;
}

ResidualGateResult evaluateTransientResiduals(const CalibrationDataset& dataset,
                                              const std::vector<TransientModelSummary>& modelSummaries,
                                              const TransientResidualOptions& options)
{
    return evaluateTransientResiduals(dataset.transientSummaries, modelSummaries, options);
}

ResidualGateResult evaluateTransientResiduals(const std::vector<TransientGoldenSummary>& goldenSummaries,
                                              const std::vector<TransientModelSummary>& modelSummaries,
                                              const TransientResidualOptions& options)
{
    ResidualGateResult gate {};
    gate.gateName = "transient residuals";
    gate.validInput = !goldenSummaries.empty()
        && options.voltageToleranceVolt > 0.0
        && options.timeToleranceSeconds > 0.0;

    if (!gate.validInput) {
        gate.failures.push_back("transient residuals require golden summaries and positive tolerances");
        finishGate(gate);
        return gate;
    }

    for (const auto& golden : goldenSummaries) {
        ResidualSet set {};
        set.name = golden.caseName;
        set.validInput = true;
        set.evaluated = true;

        const auto* model = findModelSummary(modelSummaries, golden.caseName);
        if (model == nullptr) {
            set.failures.push_back("missing transient model summary for case " + golden.caseName);
            gate.sets.push_back(std::move(set));
            continue;
        }

        addTransientPoint(set,
                          "maxDriveVolt",
                          golden.maxDriveVolt,
                          model->hasMaxDriveVolt,
                          model->maxDriveVolt,
                          options.voltageToleranceVolt,
                          "V",
                          false);
        addTransientPoint(set,
                          "maxCmdVolt",
                          golden.maxCmdVolt,
                          model->hasMaxCmdVolt,
                          model->maxCmdVolt,
                          options.voltageToleranceVolt,
                          "V",
                          false);
        addTransientPoint(set,
                          "attack90TimeSeconds",
                          golden.attack90TimeSeconds,
                          model->hasAttack90TimeSeconds,
                          model->attack90TimeSeconds,
                          options.timeToleranceSeconds,
                          "s",
                          false);

        if (golden.hasRelease10Time) {
            addTransientPoint(set,
                              "release10TimeSeconds",
                              golden.release10TimeSeconds,
                              model->hasRelease10TimeSeconds,
                              model->release10TimeSeconds,
                              options.timeToleranceSeconds,
                              "s",
                              options.requireRelease10TimeSeconds);
        } else {
            set.points.push_back(makeSkippedPoint("release10TimeSeconds", golden.caseName, "s"));
            set.notes.push_back("golden release10TimeSeconds is unavailable for " + golden.caseName);
        }

        gate.sets.push_back(std::move(set));
    }

    finishGate(gate);
    return gate;
}

} // namespace u273::reference::calibration
