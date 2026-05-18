#include "u273/reference/calibration/B6SmallSignalAcReference.h"

#include <cmath>
#include <limits>
#include <string>

#include "u273/reference/state_space/StateSpaceSolver.h"
#include "u273/reference/state_space/U273ReferenceCircuitBuilder.h"

namespace u273::reference::calibration {

namespace {

namespace ss = u273::reference::state_space;

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

} // namespace

AcLinearizationResult solveB6SmallSignalAcReference(const ss::CircuitGraph& dcCircuit,
                                                    const OperatingPointResult& dcOperatingPoint,
                                                    double commandPortResistanceOhm,
                                                    const std::vector<double>& frequenciesHz)
{
    if (!dcOperatingPoint.converged) {
        AcLinearizationResult failed {};
        failed.failures.push_back("B6 AC reference requires a converged DC operating point");
        return failed;
    }

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
                                     frequenciesHz);
}

} // namespace u273::reference::calibration
