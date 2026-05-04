#pragma once

#include "u273/reference/state_space/CircuitGraph.h"

namespace u273::reference::state_space {

// Explicit transformer boundary metadata keeps the state-space skeleton honest:
// magnetic modeling is recorded, not silently approximated.
struct TransformerBoundary {
    const char* id {};
    const char* role {};
    double idealTurnsRatio {};
    double nominalSourceResistanceOhm {};
    double nominalLoadResistanceOhm {};
};

// Factory helpers for verification fixtures and guarded U273 circuit skeletons.
class U273ReferenceCircuitBuilder {
public:
    [[nodiscard]] static CircuitGraph buildRcLowpassFixture(double inputVoltage,
                                                            double resistanceOhm,
                                                            double capacitanceFarad);
    [[nodiscard]] static CircuitGraph buildDiodeCurrentFixture(double currentAmp, DiodeModel diode);
    [[nodiscard]] static CircuitGraph buildBjtBiasFixture(double supplyVoltage,
                                                          double baseVoltage,
                                                          double collectorResistanceOhm,
                                                          double emitterResistanceOhm,
                                                          NpnBjtModel bjt);
    [[nodiscard]] static CircuitGraph buildB6BridgeSkeleton(double audioInputVoltage,
                                                            double commandVoltage);

    [[nodiscard]] static TransformerBoundary inputTransformerBoundary() noexcept;
    [[nodiscard]] static TransformerBoundary outputTransformerBoundary() noexcept;
};

} // namespace u273::reference::state_space
