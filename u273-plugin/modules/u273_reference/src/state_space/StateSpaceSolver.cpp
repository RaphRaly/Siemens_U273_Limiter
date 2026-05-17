#include "u273/reference/state_space/StateSpaceSolver.h"

#include <array>
#include <cmath>
#include <utility>

namespace u273::reference::state_space {

namespace {

bool isFinite(double value) noexcept
{
    return std::isfinite(value);
}

bool vectorIsFinite(const Vector& values) noexcept
{
    for (const auto value : values) {
        if (!isFinite(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

struct StateSpaceSolver::AssemblyContext {
    // Scratch view passed through stamping passes so each pass stays focused on
    // one component family and shares the same residual/Jacobian buffers.
    const Vector& unknowns;
    const CircuitState& previousState;
    const SolverOptions& options;
    const Vector* trBdf2StageVoltages {};
    Vector& residual;
    DenseMatrix& jacobian;
};

bool SolverOptions::isValid() const noexcept
{
    return isFinite(sampleRate)
        && sampleRate > 0.0
        && newton.maxIterations > 0
        && newton.residualTolerance > 0.0
        && newton.deltaTolerance > 0.0
        && newton.minDamping > 0.0
        && newton.minDamping <= 1.0
        && newton.pivotFloor > 0.0;
}

double SolverOptions::timeStepSeconds() const noexcept
{
    return 1.0 / sampleRate;
}

bool CircuitState::isValidFor(const CircuitGraph& circuit) const noexcept
{
    return unknowns.size() == static_cast<std::size_t>(circuit.unknownCount())
        && capacitorVoltages.size() == circuit.capacitors().size()
        && capacitorCurrents.size() == circuit.capacitors().size()
        && vectorIsFinite(unknowns)
        && vectorIsFinite(capacitorVoltages)
        && vectorIsFinite(capacitorCurrents);
}

bool StepResult::isValid() const noexcept
{
    return boundary != u273::core::ModelBoundary::unknown
        && iterations >= 0
        && stageCount > 0
        && failedStage >= 0
        && residualNorm >= 0.0
        && deltaNorm >= 0.0
        && timeStepSeconds >= 0.0;
}

StateSpaceSolver::StateSpaceSolver(CircuitGraph circuit)
    : circuit_(std::move(circuit))
{
}

CircuitState StateSpaceSolver::createInitialState() const
{
    CircuitState state {};
    state.unknowns.assign(static_cast<std::size_t>(circuit_.unknownCount()), 0.0);
    state.capacitorVoltages.assign(circuit_.capacitors().size(), 0.0);
    state.capacitorCurrents.assign(circuit_.capacitors().size(), 0.0);
    return state;
}

StepResult StateSpaceSolver::step(CircuitState& state, const SolverOptions& options) const
{
    if (options.method == IntegrationMethod::trBdf2) {
        return stepTrBdf2(state, options);
    }
    return stepSingleStage(state, options, nullptr);
}

StepResult StateSpaceSolver::stepSingleStage(CircuitState& state,
                                             const SolverOptions& options,
                                             const Vector* trBdf2StageVoltages) const
{
    StepResult result {};
    result.timeStepSeconds = options.isValid() ? options.timeStepSeconds() : 0.0;
    result.validInput = options.isValid() && state.isValidFor(circuit_);

    if (!result.validInput) {
        return result;
    }

    auto nextUnknowns = state.unknowns;
    const NewtonSolver solver {options.newton};
    const auto newtonResult = solver.solve(
        nextUnknowns,
        [this, &state, &options, trBdf2StageVoltages](
            const Vector& unknowns,
            Vector& residual,
            DenseMatrix& jacobian) {
            assembleResidualJacobian(unknowns, state, options, trBdf2StageVoltages, residual, jacobian);
        });

    result.converged = newtonResult.converged;
    result.stage1Converged = newtonResult.converged;
    result.iterations = newtonResult.iterations;
    result.residualNorm = newtonResult.residualNorm;
    result.deltaNorm = newtonResult.deltaNorm;

    if (!newtonResult.converged) {
        return result;
    }

    auto previousCapacitorVoltages = state.capacitorVoltages;
    auto previousCapacitorCurrents = state.capacitorCurrents;
    state.unknowns = std::move(nextUnknowns);
    updateCapacitorMemory(state,
                          previousCapacitorVoltages,
                          previousCapacitorCurrents,
                          options,
                          trBdf2StageVoltages);

    return result;
}

StepResult StateSpaceSolver::stepTrBdf2(CircuitState& state, const SolverOptions& options) const
{
    constexpr auto gamma = 2.0 - 1.4142135623730950488016887242097;

    StepResult result {};
    result.timeStepSeconds = options.isValid() ? options.timeStepSeconds() : 0.0;
    result.validInput = options.isValid() && state.isValidFor(circuit_);
    result.stageCount = 2;
    if (!result.validInput) {
        return result;
    }

    const auto originalState = state;

    auto stageOptions = options;
    stageOptions.method = IntegrationMethod::trapezoidal;
    stageOptions.sampleRate = options.sampleRate / gamma;

    auto stageState = originalState;
    const auto stage1 = stepSingleStage(stageState, stageOptions, nullptr);
    result.stage1Converged = stage1.validInput && stage1.converged;
    result.iterations = stage1.iterations;
    result.residualNorm = stage1.residualNorm;
    result.deltaNorm = stage1.deltaNorm;
    if (!result.stage1Converged) {
        result.failedStage = 1;
        return result;
    }

    auto finalOptions = options;
    finalOptions.method = IntegrationMethod::trBdf2;

    auto finalState = originalState;
    finalState.unknowns = stageState.unknowns;
    const auto stage2 = stepSingleStage(finalState, finalOptions, &stageState.capacitorVoltages);
    result.stage2Converged = stage2.validInput && stage2.converged;
    result.iterations += stage2.iterations;
    result.residualNorm = std::max(result.residualNorm, stage2.residualNorm);
    result.deltaNorm = std::max(result.deltaNorm, stage2.deltaNorm);
    if (!result.stage2Converged) {
        result.failedStage = 2;
        return result;
    }

    result.converged = true;
    state = std::move(finalState);
    return result;
}

double StateSpaceSolver::nodeVoltage(const CircuitState& state, NodeId node) const noexcept
{
    if (!state.isValidFor(circuit_)) {
        return 0.0;
    }
    return voltageFromUnknowns(state.unknowns, node);
}

int StateSpaceSolver::nodeUnknownIndex(NodeId node) const noexcept
{
    if (node.value <= 0 || node.value >= circuit_.nodeCount()) {
        return -1;
    }
    return node.value - 1;
}

int StateSpaceSolver::voltageSourceUnknownIndex(std::size_t sourceIndex) const noexcept
{
    return circuit_.nodeCount() - 1 + static_cast<int>(sourceIndex);
}

double StateSpaceSolver::voltageFromUnknowns(const Vector& unknowns, NodeId node) const noexcept
{
    const auto index = nodeUnknownIndex(node);
    if (index < 0 || index >= static_cast<int>(unknowns.size())) {
        return 0.0;
    }
    return unknowns[static_cast<std::size_t>(index)];
}

StateSpaceSolver::CapacitorCompanion StateSpaceSolver::capacitorCompanion(
    const CircuitState& previousState,
    std::size_t capacitorIndex,
    const SolverOptions& options,
    const Vector* trBdf2StageVoltages) const noexcept
{
    // Implicit integration stamps capacitors as a conductance plus a history
    // current. TR-BDF2 uses the trapezoidal companion in stage 1 and a
    // non-uniform BDF2 companion in stage 2.
    const auto& capacitor = circuit_.capacitors()[capacitorIndex];
    const auto h = options.timeStepSeconds();

    auto companion = CapacitorCompanion {
        capacitor.capacitanceFarad / h,
        -capacitor.capacitanceFarad / h * previousState.capacitorVoltages[capacitorIndex]};

    if (options.method == IntegrationMethod::trBdf2 && trBdf2StageVoltages != nullptr) {
        constexpr auto gamma = 2.0 - 1.4142135623730950488016887242097;
        const auto startVoltage = previousState.capacitorVoltages[capacitorIndex];
        const auto stageVoltage = (*trBdf2StageVoltages)[capacitorIndex];
        companion.conductance = capacitor.capacitanceFarad / h * ((2.0 - gamma) / (1.0 - gamma));
        companion.history = capacitor.capacitanceFarad / h
            * (((1.0 - gamma) / gamma) * startVoltage
               - (1.0 / (gamma * (1.0 - gamma))) * stageVoltage);
        return companion;
    }

    if (options.method == IntegrationMethod::trapezoidal
        || options.method == IntegrationMethod::implicitMidpoint) {
        companion.conductance = 2.0 * capacitor.capacitanceFarad / h;
        companion.history = -companion.conductance * previousState.capacitorVoltages[capacitorIndex]
            - previousState.capacitorCurrents[capacitorIndex];
    }

    return companion;
}

void StateSpaceSolver::addNodeResidual(Vector& residual, NodeId node, double value) const
{
    const auto row = nodeUnknownIndex(node);
    if (row >= 0) {
        residual[static_cast<std::size_t>(row)] += value;
    }
}

void StateSpaceSolver::addNodeJacobian(DenseMatrix& jacobian, NodeId rowNode, NodeId columnNode, double value) const
{
    const auto row = nodeUnknownIndex(rowNode);
    const auto column = nodeUnknownIndex(columnNode);
    if (row >= 0 && column >= 0) {
        jacobian.at(static_cast<std::size_t>(row), static_cast<std::size_t>(column)) += value;
    }
}

void StateSpaceSolver::stampConductance(Vector& residual,
                                        DenseMatrix& jacobian,
                                        NodeId positive,
                                        NodeId negative,
                                        double voltagePositiveNegative,
                                        double conductance,
                                        double historyCurrent) const
{
    const auto current = conductance * voltagePositiveNegative + historyCurrent;

    addNodeResidual(residual, positive, current);
    addNodeResidual(residual, negative, -current);

    addNodeJacobian(jacobian, positive, positive, conductance);
    addNodeJacobian(jacobian, positive, negative, -conductance);
    addNodeJacobian(jacobian, negative, positive, -conductance);
    addNodeJacobian(jacobian, negative, negative, conductance);
}

void StateSpaceSolver::updateCapacitorMemory(CircuitState& state,
                                             const Vector& previousCapacitorVoltages,
                                             const Vector& previousCapacitorCurrents,
                                             const SolverOptions& options,
                                             const Vector* trBdf2StageVoltages) const
{
    // After Newton convergence, persist capacitor voltage/current for the next
    // implicit step using the same companion equation used during stamping.
    CircuitState previousState {};
    previousState.unknowns = state.unknowns;
    previousState.capacitorVoltages = previousCapacitorVoltages;
    previousState.capacitorCurrents = previousCapacitorCurrents;

    for (std::size_t index = 0; index < circuit_.capacitors().size(); ++index) {
        const auto& capacitor = circuit_.capacitors()[index];
        const auto voltage = voltageFromUnknowns(state.unknowns, capacitor.positive)
            - voltageFromUnknowns(state.unknowns, capacitor.negative);
        const auto companion = capacitorCompanion(previousState, index, options, trBdf2StageVoltages);

        state.capacitorVoltages[index] = voltage;
        state.capacitorCurrents[index] = companion.conductance * voltage + companion.history;
    }
}

void StateSpaceSolver::stampResistors(AssemblyContext& assembly) const
{
    for (const auto& resistor : circuit_.resistors()) {
        if (resistor.resistanceOhm <= 0.0) {
            continue;
        }

        const auto voltage = voltageFromUnknowns(assembly.unknowns, resistor.positive)
            - voltageFromUnknowns(assembly.unknowns, resistor.negative);
        stampConductance(assembly.residual,
                         assembly.jacobian,
                         resistor.positive,
                         resistor.negative,
                         voltage,
                         1.0 / resistor.resistanceOhm,
                         0.0);
    }
}

void StateSpaceSolver::stampCapacitors(AssemblyContext& assembly) const
{
    for (std::size_t index = 0; index < circuit_.capacitors().size(); ++index) {
        const auto& capacitor = circuit_.capacitors()[index];
        if (capacitor.capacitanceFarad <= 0.0) {
            continue;
        }

        const auto voltage = voltageFromUnknowns(assembly.unknowns, capacitor.positive)
            - voltageFromUnknowns(assembly.unknowns, capacitor.negative);
        const auto companion = capacitorCompanion(assembly.previousState,
                                                 index,
                                                 assembly.options,
                                                 assembly.trBdf2StageVoltages);
        stampConductance(assembly.residual,
                         assembly.jacobian,
                         capacitor.positive,
                         capacitor.negative,
                         voltage,
                         companion.conductance,
                         companion.history);
    }
}

void StateSpaceSolver::stampCurrentSources(AssemblyContext& assembly) const
{
    for (const auto& source : circuit_.currentSources()) {
        addNodeResidual(assembly.residual, source.positive, source.currentAmp);
        addNodeResidual(assembly.residual, source.negative, -source.currentAmp);
    }
}

void StateSpaceSolver::stampVoltageSources(AssemblyContext& assembly) const
{
    for (std::size_t index = 0; index < circuit_.voltageSources().size(); ++index) {
        const auto& source = circuit_.voltageSources()[index];
        const auto currentIndex = voltageSourceUnknownIndex(index);
        const auto current = assembly.unknowns[static_cast<std::size_t>(currentIndex)];

        addNodeResidual(assembly.residual, source.positive, current);
        addNodeResidual(assembly.residual, source.negative, -current);

        const auto positiveRow = nodeUnknownIndex(source.positive);
        const auto negativeRow = nodeUnknownIndex(source.negative);
        if (positiveRow >= 0) {
            assembly.jacobian.at(static_cast<std::size_t>(positiveRow), static_cast<std::size_t>(currentIndex)) += 1.0;
        }
        if (negativeRow >= 0) {
            assembly.jacobian.at(static_cast<std::size_t>(negativeRow), static_cast<std::size_t>(currentIndex)) -= 1.0;
        }

        const auto equationRow = static_cast<std::size_t>(currentIndex);
        const auto sourceVoltage = voltageFromUnknowns(assembly.unknowns, source.positive)
            - voltageFromUnknowns(assembly.unknowns, source.negative);
        assembly.residual[equationRow] += sourceVoltage - source.voltage;

        const auto positiveColumn = nodeUnknownIndex(source.positive);
        const auto negativeColumn = nodeUnknownIndex(source.negative);
        if (positiveColumn >= 0) {
            assembly.jacobian.at(equationRow, static_cast<std::size_t>(positiveColumn)) += 1.0;
        }
        if (negativeColumn >= 0) {
            assembly.jacobian.at(equationRow, static_cast<std::size_t>(negativeColumn)) -= 1.0;
        }
    }
}

void StateSpaceSolver::stampDiodes(AssemblyContext& assembly) const
{
    for (const auto& diode : circuit_.diodes()) {
        const auto voltage = voltageFromUnknowns(assembly.unknowns, diode.anode)
            - voltageFromUnknowns(assembly.unknowns, diode.cathode);
        const auto current = diode.model.currentAmp(voltage);
        const auto conductance = diode.model.conductanceSiemens(voltage);

        addNodeResidual(assembly.residual, diode.anode, current);
        addNodeResidual(assembly.residual, diode.cathode, -current);

        addNodeJacobian(assembly.jacobian, diode.anode, diode.anode, conductance);
        addNodeJacobian(assembly.jacobian, diode.anode, diode.cathode, -conductance);
        addNodeJacobian(assembly.jacobian, diode.cathode, diode.anode, -conductance);
        addNodeJacobian(assembly.jacobian, diode.cathode, diode.cathode, conductance);
    }
}

void StateSpaceSolver::stampNpnBjts(AssemblyContext& assembly) const
{
    for (const auto& bjt : circuit_.npnBjts()) {
        const auto collectorVolt = voltageFromUnknowns(assembly.unknowns, bjt.collector);
        const auto baseVolt = voltageFromUnknowns(assembly.unknowns, bjt.base);
        const auto emitterVolt = voltageFromUnknowns(assembly.unknowns, bjt.emitter);
        const auto evaluation = evaluateNpnEbersMoll(bjt.model, collectorVolt, baseVolt, emitterVolt);
        const std::array<NodeId, 3> terminals {bjt.collector, bjt.base, bjt.emitter};

        for (std::size_t row = 0; row < terminals.size(); ++row) {
            addNodeResidual(assembly.residual, terminals[row], evaluation.currentsAmp[row]);
            for (std::size_t column = 0; column < terminals.size(); ++column) {
                addNodeJacobian(assembly.jacobian,
                                terminals[row],
                                terminals[column],
                                evaluation.dCurrentDVoltage[row][column]);
            }
        }
    }
}

void StateSpaceSolver::assembleResidualJacobian(const Vector& unknowns,
                                                const CircuitState& previousState,
                                                const SolverOptions& options,
                                                const Vector* trBdf2StageVoltages,
                                                Vector& residual,
                                                DenseMatrix& jacobian) const
{
    const auto unknownCount = static_cast<std::size_t>(circuit_.unknownCount());
    residual.assign(unknownCount, 0.0);
    jacobian.resize(unknownCount, unknownCount, 0.0);

    // Keep assembly order explicit; individual component equations live in
    // small stamping passes instead of one large type-dispatch method.
    AssemblyContext assembly {unknowns, previousState, options, trBdf2StageVoltages, residual, jacobian};
    stampResistors(assembly);
    stampCapacitors(assembly);
    stampCurrentSources(assembly);
    stampVoltageSources(assembly);
    stampDiodes(assembly);
    stampNpnBjts(assembly);
}

} // namespace u273::reference::state_space
