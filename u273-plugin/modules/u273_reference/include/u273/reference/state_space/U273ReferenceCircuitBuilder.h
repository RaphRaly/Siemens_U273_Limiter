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

struct B6BridgeSmallSignalAcOptions {
    double sourceAmplitudeVolt {1.0};
    double commandPortResistanceOhm {10000.0};
    double r5Ohm {5600.0};
    double r6Ohm {39000.0};
    double r7EffectiveOhm {100.0};
    double r8EffectiveOhm {250000.0};
    double r9Ohm {390000.0};
    double r10Ohm {20000.0};
    double r11Ohm {39000.0};
    double c1Farad {0.68e-6};
    double c2Farad {22.0e-6};
    double c3Farad {4.7e-6};
    double c4AbglFarad {};
    double c5Farad {150.0e-6};
    double c6AbglFarad {};
    double c7Farad {4.7e-6};
    double d1ConductanceSiemens {1.0e-12};
    double d2ConductanceSiemens {1.0e-12};
    double d3ConductanceSiemens {1.0e-12};
    double d4ConductanceSiemens {1.0e-12};
    double gminResistanceOhm {1.0e12};
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
    [[nodiscard]] static CircuitGraph buildB6BridgeSmallSignalAcReference(
        const B6BridgeSmallSignalAcOptions& options = {});

    [[nodiscard]] static TransformerBoundary inputTransformerBoundary() noexcept;
    [[nodiscard]] static TransformerBoundary outputTransformerBoundary() noexcept;
};

} // namespace u273::reference::state_space
