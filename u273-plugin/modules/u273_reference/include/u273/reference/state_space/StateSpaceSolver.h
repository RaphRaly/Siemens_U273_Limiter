#pragma once

#include "u273/core/ModelBoundary.h"
#include "u273/reference/state_space/CircuitGraph.h"
#include "u273/reference/state_space/NewtonSolver.h"

namespace u273::reference::state_space {

enum class IntegrationMethod {
    backwardEuler = 0,
    trapezoidal,
    implicitMidpoint,
    trBdf2
};

// Solver settings for offline implicit integration. Defaults favor stable
// reference validation over realtime cost.
struct SolverOptions {
    double sampleRate {192000.0};
    IntegrationMethod method {IntegrationMethod::backwardEuler};
    NewtonOptions newton {};

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] double timeStepSeconds() const noexcept;
};

// Mutable state carried from one implicit step to the next.
struct CircuitState {
    Vector unknowns {};
    Vector capacitorVoltages {};
    Vector capacitorCurrents {};

    [[nodiscard]] bool isValidFor(const CircuitGraph& circuit) const noexcept;
};

// Step diagnostics are part of the scientific boundary: convergence and
// residuals must be visible to tests and reports.
struct StepResult {
    u273::core::ModelBoundary boundary {u273::core::ModelBoundary::fullActiveModelUnverified};
    bool converged {};
    bool validInput {};
    int iterations {};
    double residualNorm {};
    double deltaNorm {};
    double timeStepSeconds {};

    [[nodiscard]] bool isValid() const noexcept;
};

// Offline component-level state-space solver. It is intentionally separate from
// the realtime plugin path.
class StateSpaceSolver {
public:
    explicit StateSpaceSolver(CircuitGraph circuit);

    [[nodiscard]] CircuitState createInitialState() const;
    [[nodiscard]] StepResult step(CircuitState& state, const SolverOptions& options) const;
    [[nodiscard]] double nodeVoltage(const CircuitState& state, NodeId node) const noexcept;
    [[nodiscard]] const CircuitGraph& circuit() const noexcept { return circuit_; }

private:
    struct AssemblyContext;
    struct CapacitorCompanion {
        double conductance {};
        double history {};
    };

    [[nodiscard]] int nodeUnknownIndex(NodeId node) const noexcept;
    [[nodiscard]] int voltageSourceUnknownIndex(std::size_t sourceIndex) const noexcept;
    [[nodiscard]] double voltageFromUnknowns(const Vector& unknowns, NodeId node) const noexcept;
    [[nodiscard]] CapacitorCompanion capacitorCompanion(const CircuitState& previousState,
                                                        std::size_t capacitorIndex,
                                                        const SolverOptions& options) const noexcept;

    void addNodeResidual(Vector& residual, NodeId node, double value) const;
    void addNodeJacobian(DenseMatrix& jacobian, NodeId rowNode, NodeId columnNode, double value) const;
    void stampConductance(Vector& residual,
                          DenseMatrix& jacobian,
                          NodeId positive,
                          NodeId negative,
                          double voltagePositiveNegative,
                          double conductance,
                          double historyCurrent) const;
    void updateCapacitorMemory(CircuitState& state,
                               const Vector& previousCapacitorVoltages,
                               const Vector& previousCapacitorCurrents,
                               const SolverOptions& options) const;
    void stampResistors(AssemblyContext& assembly) const;
    void stampCapacitors(AssemblyContext& assembly) const;
    void stampCurrentSources(AssemblyContext& assembly) const;
    void stampVoltageSources(AssemblyContext& assembly) const;
    void stampDiodes(AssemblyContext& assembly) const;
    void stampNpnBjts(AssemblyContext& assembly) const;
    void assembleResidualJacobian(const Vector& unknowns,
                                  const CircuitState& previousState,
                                  const SolverOptions& options,
                                  Vector& residual,
                                  DenseMatrix& jacobian) const;

    CircuitGraph circuit_ {};
};

} // namespace u273::reference::state_space
