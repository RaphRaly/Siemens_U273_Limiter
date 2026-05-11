#include "u273/reference/calibration/OperatingPointSolver.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

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

[[nodiscard]] ss::CircuitGraph cloneCircuit(const ss::CircuitGraph& source,
                                            double sourceScale,
                                            double nodeGminSiemens)
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
                                currentSource.currentAmp * sourceScale);
    }

    for (const auto& voltageSource : source.voltageSources()) {
        target.addVoltageSource(voltageSource.id,
                                sameNode(source, target, voltageSource.positive),
                                sameNode(source, target, voltageSource.negative),
                                voltageSource.voltage * sourceScale);
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

    if (nodeGminSiemens > 0.0 && std::isfinite(nodeGminSiemens)) {
        const auto resistanceOhm = 1.0 / nodeGminSiemens;
        for (auto node = 1; node < source.nodeCount(); ++node) {
            target.addResistor("OP_GMIN_" + source.nodeName(ss::NodeId {node}),
                               ss::NodeId {node},
                               ss::kGroundNode,
                               resistanceOhm);
        }
    }

    return target;
}

[[nodiscard]] OperatingPointAttemptLog makeAttemptLog(std::string strategy,
                                                      const ss::StepResult& step,
                                                      bool converged,
                                                      int continuationSteps)
{
    OperatingPointAttemptLog log {};
    log.strategy = std::move(strategy);
    log.iterations = step.iterations;
    log.residualNorm = step.residualNorm;
    log.converged = converged;
    log.continuationSteps = continuationSteps;
    return log;
}

[[nodiscard]] ss::CircuitState createAttemptState(const ss::StateSpaceSolver& solver,
                                                  const ss::Vector& initialUnknowns)
{
    auto state = solver.createInitialState();
    if (initialUnknowns.size() == state.unknowns.size()) {
        state.unknowns = initialUnknowns;
    }
    return state;
}

[[nodiscard]] bool solveSingleCircuit(const ss::CircuitGraph& circuit,
                                      const ss::Vector& initialUnknowns,
                                      const ss::SolverOptions& options,
                                      ss::CircuitState& solvedState,
                                      ss::StepResult& solvedStep)
{
    const ss::StateSpaceSolver solver {circuit};
    auto state = createAttemptState(solver, initialUnknowns);
    const auto step = solver.step(state, options);
    solvedState = std::move(state);
    solvedStep = step;
    return step.validInput && step.converged;
}

} // namespace

bool DCCircuitView::isValid() const noexcept
{
    return circuit.nodeCount() > 0
        && omittedCapacitors >= 0
        && preservedResistors >= 0
        && preservedDiodes >= 0
        && preservedBjts >= 0;
}

DCCircuitView DCCircuitView::fromCircuit(const ss::CircuitGraph& source)
{
    DCCircuitView view {};
    copyNodes(source, view.circuit);

    for (const auto& resistor : source.resistors()) {
        view.circuit.addResistor(resistor.id,
                                 sameNode(source, view.circuit, resistor.positive),
                                 sameNode(source, view.circuit, resistor.negative),
                                 resistor.resistanceOhm);
        ++view.preservedResistors;
    }

    view.omittedCapacitors = static_cast<int>(source.capacitors().size());

    for (const auto& currentSource : source.currentSources()) {
        view.circuit.addCurrentSource(currentSource.id,
                                      sameNode(source, view.circuit, currentSource.positive),
                                      sameNode(source, view.circuit, currentSource.negative),
                                      currentSource.currentAmp);
    }

    for (const auto& voltageSource : source.voltageSources()) {
        view.circuit.addVoltageSource(voltageSource.id,
                                      sameNode(source, view.circuit, voltageSource.positive),
                                      sameNode(source, view.circuit, voltageSource.negative),
                                      voltageSource.voltage);
    }

    for (const auto& diode : source.diodes()) {
        view.circuit.addDiode(diode.id,
                              sameNode(source, view.circuit, diode.anode),
                              sameNode(source, view.circuit, diode.cathode),
                              diode.model);
        ++view.preservedDiodes;
    }

    for (const auto& bjt : source.npnBjts()) {
        view.circuit.addNpnBjt(bjt.id,
                               sameNode(source, view.circuit, bjt.collector),
                               sameNode(source, view.circuit, bjt.base),
                               sameNode(source, view.circuit, bjt.emitter),
                               bjt.model);
        ++view.preservedBjts;
    }

    for (const auto& transformer : source.idealTransformerPorts()) {
        view.circuit.addIdealTransformerPort(transformer.id,
                                             sameNode(source, view.circuit, transformer.primaryPositive),
                                             sameNode(source, view.circuit, transformer.primaryNegative),
                                             sameNode(source, view.circuit, transformer.secondaryPositive),
                                             sameNode(source, view.circuit, transformer.secondaryNegative),
                                             transformer.turnsRatio,
                                             transformer.sourceResistanceOhm,
                                             transformer.loadResistanceOhm);
    }

    return view;
}

OperatingPointSolver::OperatingPointSolver(OperatingPointOptions options)
    : options_(std::move(options))
{
}

OperatingPointResult OperatingPointSolver::solve(const DCCircuitView& view) const
{
    return solve(view, {});
}

OperatingPointResult OperatingPointSolver::solve(const DCCircuitView& view, const ss::Vector& initialUnknowns) const
{
    OperatingPointResult result {};
    result.validInput = view.isValid()
        && options_.solver.isValid()
        && options_.maxAttempts > 0
        && options_.gminSteps > 0
        && options_.sourceSteps > 0
        && options_.initialGminSiemens > 0.0
        && options_.finalGminSiemens > 0.0
        && options_.initialGminSiemens >= options_.finalGminSiemens
        && options_.initialSourceScale > 0.0
        && options_.initialSourceScale <= 1.0;
    if (!result.validInput) {
        result.failures.push_back("invalid operating point input");
        return result;
    }

    auto attemptCount = 0;
    auto runAttempt = [&result, &attemptCount, this](std::string strategy,
                                                    const ss::CircuitGraph& circuit,
                                                    const ss::Vector& startUnknowns,
                                                    ss::SolverOptions attemptOptions,
                                                    int continuationSteps = 0) {
        ++attemptCount;
        attemptOptions.method = ss::IntegrationMethod::backwardEuler;

        ss::CircuitState state {};
        ss::StepResult step {};
        const auto converged = solveSingleCircuit(circuit, startUnknowns, attemptOptions, state, step);
        result.attempts = attemptCount;
        result.step = step;
        result.state = std::move(state);
        result.converged = converged;
        result.attemptLog.push_back(makeAttemptLog(std::move(strategy), step, converged, continuationSteps));
        return converged;
    };

    ss::SolverOptions directOptions = options_.solver;
    if (runAttempt("direct Newton", view.circuit, initialUnknowns, directOptions)) {
        return result;
    }

    if (attemptCount < options_.maxAttempts && options_.enableDampedRetry) {
        auto dampedOptions = options_.solver;
        dampedOptions.newton.maxIterations = std::max(dampedOptions.newton.maxIterations * 2, 16);
        dampedOptions.newton.minDamping = std::max(dampedOptions.newton.minDamping * 0.25, 1.0e-6);
        if (runAttempt("stronger damping", view.circuit, initialUnknowns, dampedOptions)) {
            return result;
        }
    }

    auto runGminStepping = [&result, this, &attemptCount, &initialUnknowns, &view]() {
        ++attemptCount;
        auto currentUnknowns = initialUnknowns;
        ss::CircuitState finalState {};
        ss::StepResult finalStep {};
        auto continuationSteps = 0;

        for (auto stepIndex = 0; stepIndex < options_.gminSteps; ++stepIndex) {
            const auto ratio = options_.gminSteps == 1
                ? 1.0
                : static_cast<double>(stepIndex) / static_cast<double>(options_.gminSteps - 1);
            const auto logGmin = std::log(options_.initialGminSiemens)
                + ratio * (std::log(options_.finalGminSiemens) - std::log(options_.initialGminSiemens));
            const auto gmin = std::exp(logGmin);
            const auto steppedCircuit = cloneCircuit(view.circuit, 1.0, gmin);

            auto options = options_.solver;
            options.method = ss::IntegrationMethod::backwardEuler;
            options.newton.maxIterations = std::max(options.newton.maxIterations * 2, 32);
            const auto converged = solveSingleCircuit(steppedCircuit, currentUnknowns, options, finalState, finalStep);
            ++continuationSteps;
            currentUnknowns = finalState.unknowns;
            if (!converged) {
                result.attempts = attemptCount;
                result.step = finalStep;
                result.state = std::move(finalState);
                result.converged = false;
                result.attemptLog.push_back(makeAttemptLog("gmin stepping", finalStep, false, continuationSteps));
                return false;
            }
        }

        auto finalOptions = options_.solver;
        finalOptions.method = ss::IntegrationMethod::backwardEuler;
        finalOptions.newton.maxIterations = std::max(finalOptions.newton.maxIterations * 2, 32);
        const auto converged = solveSingleCircuit(view.circuit, currentUnknowns, finalOptions, finalState, finalStep);
        ++continuationSteps;
        result.attempts = attemptCount;
        result.step = finalStep;
        result.state = std::move(finalState);
        result.converged = converged;
        result.attemptLog.push_back(makeAttemptLog("gmin stepping", finalStep, converged, continuationSteps));
        return converged;
    };

    if (attemptCount < options_.maxAttempts && options_.enableGminStepping) {
        if (runGminStepping()) {
            return result;
        }
    }

    auto runSourceStepping = [&result, this, &attemptCount, &initialUnknowns, &view]() {
        ++attemptCount;
        auto currentUnknowns = initialUnknowns;
        ss::CircuitState finalState {};
        ss::StepResult finalStep {};
        auto continuationSteps = 0;

        for (auto stepIndex = 0; stepIndex < options_.sourceSteps; ++stepIndex) {
            const auto ratio = options_.sourceSteps == 1
                ? 1.0
                : static_cast<double>(stepIndex) / static_cast<double>(options_.sourceSteps - 1);
            const auto scale = options_.initialSourceScale + ratio * (1.0 - options_.initialSourceScale);
            const auto steppedCircuit = cloneCircuit(view.circuit, scale, 0.0);

            auto options = options_.solver;
            options.method = ss::IntegrationMethod::backwardEuler;
            options.newton.maxIterations = std::max(options.newton.maxIterations * 2, 32);
            const auto converged = solveSingleCircuit(steppedCircuit, currentUnknowns, options, finalState, finalStep);
            ++continuationSteps;
            currentUnknowns = finalState.unknowns;
            if (!converged) {
                result.attempts = attemptCount;
                result.step = finalStep;
                result.state = std::move(finalState);
                result.converged = false;
                result.attemptLog.push_back(makeAttemptLog("source stepping", finalStep, false, continuationSteps));
                return false;
            }
        }

        result.attempts = attemptCount;
        result.step = finalStep;
        result.state = std::move(finalState);
        result.converged = finalStep.validInput && finalStep.converged;
        result.attemptLog.push_back(makeAttemptLog("source stepping", finalStep, result.converged, continuationSteps));
        return result.converged;
    };

    if (attemptCount < options_.maxAttempts && options_.enableSourceStepping) {
        if (runSourceStepping()) {
            return result;
        }
    }

    result.failures.push_back("DC operating point did not converge");
    for (const auto& attempt : result.attemptLog) {
        result.failures.push_back(attempt.strategy + " failed after "
                                  + std::to_string(attempt.iterations)
                                  + " Newton iterations, residual norm "
                                  + std::to_string(attempt.residualNorm));
    }
    return result;
}

} // namespace u273::reference::calibration
