#include "u273/reference/state_space/U273ReferenceCircuitBuilder.h"

namespace u273::reference::state_space {

CircuitGraph U273ReferenceCircuitBuilder::buildRcLowpassFixture(double inputVoltage,
                                                                double resistanceOhm,
                                                                double capacitanceFarad)
{
    CircuitGraph circuit {};
    const auto input = circuit.addNode("IN");
    const auto output = circuit.addNode("OUT");

    circuit.addVoltageSource("VIN", input, kGroundNode, inputVoltage);
    circuit.addResistor("R1", input, output, resistanceOhm);
    circuit.addCapacitor("C1", output, kGroundNode, capacitanceFarad);

    return circuit;
}

CircuitGraph U273ReferenceCircuitBuilder::buildDiodeCurrentFixture(double currentAmp, DiodeModel diode)
{
    CircuitGraph circuit {};
    const auto output = circuit.addNode("DIODE_NODE");

    circuit.addCurrentSource("IIN", kGroundNode, output, currentAmp);
    circuit.addDiode("D1", output, kGroundNode, diode);

    return circuit;
}

CircuitGraph U273ReferenceCircuitBuilder::buildBjtBiasFixture(double supplyVoltage,
                                                              double baseVoltage,
                                                              double collectorResistanceOhm,
                                                              double emitterResistanceOhm,
                                                              NpnBjtModel bjt)
{
    CircuitGraph circuit {};
    const auto supply = circuit.addNode("VCC");
    const auto collector = circuit.addNode("C");
    const auto base = circuit.addNode("B");
    const auto emitter = circuit.addNode("E");

    circuit.addVoltageSource("VCC", supply, kGroundNode, supplyVoltage);
    circuit.addVoltageSource("VBASE", base, kGroundNode, baseVoltage);
    circuit.addResistor("RC", supply, collector, collectorResistanceOhm);
    circuit.addResistor("RE", emitter, kGroundNode, emitterResistanceOhm);
    circuit.addNpnBjt("Q1", collector, base, emitter, bjt);

    return circuit;
}

CircuitGraph U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(double audioInputVoltage,
                                                                double commandVoltage)
{
    CircuitGraph circuit {};
    const auto vs = circuit.addNode("B6.VS");
    const auto nx = circuit.addNode("B6.NX");
    const auto na = circuit.addNode("B6.NA");
    const auto nb = circuit.addNode("B6.NB");
    const auto command = circuit.addNode("B6.CMD");

    circuit.addVoltageSource("B6.V_AUDIO_IDEAL_U1_SECONDARY", vs, kGroundNode, audioInputVoltage);
    circuit.addVoltageSource("B6.V_COMMAND_THEVENIN", command, kGroundNode, commandVoltage);
    circuit.addResistor("B6.R5", vs, nx, 5600.0);
    circuit.addCapacitor("B6.C1", nx, na, 0.68e-6);
    circuit.addCapacitor("B6.C2", na, nb, 22.0e-6);
    circuit.addResistor("B6.R10", command, nb, 20000.0);
    circuit.addDiode("B6.D1_OA154", na, nb, makeOa154Approximation());
    circuit.addDiode("B6.D2_SSD55", nb, na, makeSsd55Approximation());
    circuit.addDiode("B6.D3_SSD55", na, kGroundNode, makeSsd55Approximation());
    circuit.addDiode("B6.D4_OA154", kGroundNode, nb, makeOa154Approximation());
    circuit.addIdealTransformerPort("U1",
                                    vs,
                                    kGroundNode,
                                    vs,
                                    kGroundNode,
                                    inputTransformerBoundary().idealTurnsRatio,
                                    inputTransformerBoundary().nominalSourceResistanceOhm,
                                    inputTransformerBoundary().nominalLoadResistanceOhm);

    return circuit;
}

TransformerBoundary U273ReferenceCircuitBuilder::inputTransformerBoundary() noexcept
{
    return TransformerBoundary {
        "U1",
        "input transformer represented as an ideal secondary port for state-space sprint 1",
        1.0,
        300.0,
        10000.0};
}

TransformerBoundary U273ReferenceCircuitBuilder::outputTransformerBoundary() noexcept
{
    return TransformerBoundary {
        "U2",
        "output transformer represented as an ideal output port for state-space sprint 1",
        1.0,
        50.0,
        300.0};
}

} // namespace u273::reference::state_space
