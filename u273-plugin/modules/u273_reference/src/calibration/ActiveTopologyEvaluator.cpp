#include "u273/reference/calibration/ActiveTopologyEvaluator.h"

#include "u273/reference/calibration/ActiveTopologyInventory.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

[[nodiscard]] bool finite(double value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] int nodeUnknownIndex(const ss::CircuitGraph& circuit, ss::NodeId node) noexcept
{
    if (node.value <= 0 || node.value >= circuit.nodeCount()) {
        return -1;
    }
    return node.value - 1;
}

[[nodiscard]] int voltageSourceUnknownIndex(const ss::CircuitGraph& circuit, std::size_t sourceIndex) noexcept
{
    return circuit.nodeCount() - 1 + static_cast<int>(sourceIndex);
}

[[nodiscard]] double voltageAt(const ss::CircuitGraph& circuit,
                               const ss::CircuitState& state,
                               ss::NodeId node) noexcept
{
    const auto index = nodeUnknownIndex(circuit, node);
    if (index < 0 || index >= static_cast<int>(state.unknowns.size())) {
        return 0.0;
    }
    return state.unknowns[static_cast<std::size_t>(index)];
}

[[nodiscard]] double terminalCurrentWithoutCandidate(const ss::CircuitGraph& circuit,
                                                     const ss::CircuitState& state,
                                                     ss::NodeId terminal)
{
    auto current = 0.0;

    for (const auto& resistor : circuit.resistors()) {
        if (!(resistor.resistanceOhm > 0.0)) {
            continue;
        }
        if (resistor.positive == terminal) {
            current += (voltageAt(circuit, state, resistor.positive)
                        - voltageAt(circuit, state, resistor.negative))
                / resistor.resistanceOhm;
        } else if (resistor.negative == terminal) {
            current += (voltageAt(circuit, state, resistor.negative)
                        - voltageAt(circuit, state, resistor.positive))
                / resistor.resistanceOhm;
        }
    }

    for (const auto& source : circuit.currentSources()) {
        if (source.positive == terminal) {
            current += source.currentAmp;
        } else if (source.negative == terminal) {
            current -= source.currentAmp;
        }
    }

    for (std::size_t index = 0; index < circuit.voltageSources().size(); ++index) {
        const auto sourceCurrentIndex = voltageSourceUnknownIndex(circuit, index);
        if (sourceCurrentIndex < 0 || sourceCurrentIndex >= static_cast<int>(state.unknowns.size())) {
            continue;
        }

        const auto sourceCurrent = state.unknowns[static_cast<std::size_t>(sourceCurrentIndex)];
        const auto& source = circuit.voltageSources()[index];
        if (source.positive == terminal) {
            current += sourceCurrent;
        } else if (source.negative == terminal) {
            current -= sourceCurrent;
        }
    }

    for (const auto& diode : circuit.diodes()) {
        const auto vd = voltageAt(circuit, state, diode.anode) - voltageAt(circuit, state, diode.cathode);
        const auto diodeCurrent = diode.model.currentAmp(vd);
        if (diode.anode == terminal) {
            current += diodeCurrent;
        } else if (diode.cathode == terminal) {
            current -= diodeCurrent;
        }
    }

    return current;
}

} // namespace

ActiveTopologyEvaluationResult ActiveTopologyEvaluator::evaluate(
    const ss::CircuitGraph& circuit,
    const OperatingPointResult& operatingPoint,
    const ActiveTopologyCandidate& candidate,
    const ActiveTopologyEvaluationOptions& options) const
{
    ActiveTopologyEvaluationResult result {};
    result.candidate = candidate;
    result.evaluated = operatingPoint.converged
        && operatingPoint.state.unknowns.size() == static_cast<std::size_t>(circuit.unknownCount())
        && options.minForwardVbeVolt > 0.0
        && options.maxForwardVbeVolt >= options.minForwardVbeVolt
        && options.minForwardVceVolt >= 0.0
        && options.localKclToleranceAmp > 0.0;

    if (!result.evaluated) {
        result.reason = "candidate evaluation requires a converged operating point and valid tolerances";
        return result;
    }

    if (!candidate.hasUsablePins()) {
        result.reason = candidate.rejectionReason.empty()
            ? "candidate pins are not usable"
            : candidate.rejectionReason;
        return result;
    }

    if (candidate.kind != ActiveDeviceKind::npn) {
        result.reason = "only NPN candidates are supported in the first guarded topology phase";
        return result;
    }

    const auto collectorVolt = voltageAt(circuit, operatingPoint.state, candidate.collector);
    const auto baseVolt = voltageAt(circuit, operatingPoint.state, candidate.base);
    const auto emitterVolt = voltageAt(circuit, operatingPoint.state, candidate.emitter);
    result.vbeVolt = baseVolt - emitterVolt;
    result.vceVolt = collectorVolt - emitterVolt;
    result.dcPlausible = finite(result.vbeVolt)
        && finite(result.vceVolt)
        && result.vbeVolt >= options.minForwardVbeVolt
        && result.vbeVolt <= options.maxForwardVbeVolt
        && result.vceVolt >= options.minForwardVceVolt;

    const auto model = ss::makeSmallSignalNpnApproximation(100.0);
    const auto bjt = ss::evaluateNpnEbersMoll(model, collectorVolt, baseVolt, emitterVolt);
    const std::array<ss::NodeId, 3> terminals {candidate.collector, candidate.base, candidate.emitter};
    auto worstKcl = 0.0;
    for (std::size_t terminal = 0; terminal < terminals.size(); ++terminal) {
        const auto existingCurrent = terminalCurrentWithoutCandidate(circuit, operatingPoint.state, terminals[terminal]);
        worstKcl = std::max(worstKcl, std::abs(existingCurrent + bjt.currentsAmp[terminal]));
    }
    result.localKclAmp = worstKcl;
    result.localKclPlausible = finite(result.localKclAmp) && result.localKclAmp <= options.localKclToleranceAmp;
    result.acImproves = !options.requireAcImprovement;
    result.accepted = result.dcPlausible && result.localKclPlausible && result.acImproves;

    if (result.accepted) {
        result.reason = "candidate passes DC plausibility and frozen local KCL checks";
    } else if (!result.dcPlausible) {
        result.reason = "candidate rejected by VBE/VCE plausibility";
    } else if (!result.localKclPlausible) {
        result.reason = "candidate rejected by frozen local KCL imbalance";
    } else {
        result.reason = "candidate remains guarded until a single-candidate AC improvement is measured";
    }

    return result;
}

std::vector<ActiveTopologyEvaluationResult> ActiveTopologyEvaluator::evaluateCandidates(
    const ss::CircuitGraph& circuit,
    const OperatingPointResult& operatingPoint,
    const std::vector<ActiveTopologyCandidate>& candidates,
    const ActiveTopologyEvaluationOptions& options) const
{
    std::vector<ActiveTopologyEvaluationResult> results {};
    results.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        results.push_back(evaluate(circuit, operatingPoint, candidate, options));
    }
    return results;
}

std::vector<ActiveTopologyEvaluationResult> ActiveTopologyEvaluator::evaluateB6GuardedCandidates(
    const ss::CircuitGraph& circuit,
    const OperatingPointResult& operatingPoint,
    const ActiveTopologyEvaluationOptions& options) const
{
    return evaluateCandidates(
        circuit,
        operatingPoint,
        instantiateActiveTopologyCandidates(circuit, defaultB6ActiveTopologyCandidateSpecs()),
        options);
}

std::vector<ActiveTopologyEvaluationResult> ActiveTopologyEvaluator::evaluateB6B11GuardedCandidates(
    const ss::CircuitGraph& circuit,
    const OperatingPointResult& operatingPoint,
    const ActiveTopologyEvaluationOptions& options) const
{
    return evaluateCandidates(
        circuit,
        operatingPoint,
        instantiateActiveTopologyCandidates(circuit, defaultB6B11ActiveTopologyCandidateSpecs()),
        options);
}

} // namespace u273::reference::calibration
