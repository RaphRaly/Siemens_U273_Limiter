#pragma once

#include <array>
#include <cmath>

namespace u273::reference {

enum class DynamicsTopology {
    diodeBridge = 0,
    variableMu,
    fet,
    optical
};

// Datasheet and peer-comparison values pinned in code so tests can catch
// accidental drift in scientific assumptions.
struct FrequencyBandSpec {
    double lowHz {};
    double highHz {};
};

struct DistortionSpec {
    double linearMaxPercent {};
    double regulatedLowFrequencyMaxPercent {};
    double regulatedMidHighMaxPercent {};
    double lowFrequencyHz {};
    double midHighStartHz {};
    double highFrequencyHz {};
};

struct TimeConstantSpec {
    double limiterAttackMs {};
    double compressorAttackMs {};
    double releaseMinMs {};
    double releaseMaxMs {};
};

struct U273TechnicalSpec {
    double inputResistanceOhm {};
    double outputResistanceOhm {};
    double loadResistanceOhm {};
    FrequencyBandSpec frequencyBand {};
    double nominalOutputVoltageRms {};
    double weightedNoiseDistanceDb {};
    DistortionSpec distortion {};
    TimeConstantSpec timeConstants {};
    double supplyVoltageDc {};
    double supplyCurrentAmp {};
};

// Peer specs define a plausibility envelope, not an assertion that U273 is a
// clone of either comparator.
struct PeerDynamicsSpec {
    const char* name {};
    int designEraYear {};
    DynamicsTopology topology {DynamicsTopology::diodeBridge};
    double inputResistanceOhm {};
    double outputResistanceOhm {};
    FrequencyBandSpec frequencyBand {};
    double fastestLimiterAttackMs {};
    double compressorAttackMs {};
    double releaseMinMs {};
    double releaseMaxMs {};
    double typicalMaxDistortionPercent {};
};

// Four-point distortion measurement used by the original Siemens-style formula.
struct FourPointDistortionCurrents {
    double i1 {};
    double i2 {};
    double i3 {};
    double i4 {};
};

[[nodiscard]] inline constexpr U273TechnicalSpec makeU273TechnicalSpec() noexcept
{
    return U273TechnicalSpec {
        10000.0,
        30.0,
        300.0,
        FrequencyBandSpec {40.0, 15000.0},
        1.55,
        70.0,
        DistortionSpec {0.5, 1.0, 0.5, 40.0, 1000.0, 15000.0},
        TimeConstantSpec {0.5, 1.0, 500.0, 1500.0},
        24.0,
        0.05};
}

[[nodiscard]] inline constexpr std::array<PeerDynamicsSpec, 2> makeDiodeBridgePeerSpecs() noexcept
{
    return {{
        PeerDynamicsSpec {
            "AMS Neve 2254/R",
            1969,
            DynamicsTopology::diodeBridge,
            10000.0,
            80.0,
            FrequencyBandSpec {20.0, 20000.0},
            0.1,
            5.0,
            100.0,
            1500.0,
            0.4},
        PeerDynamicsSpec {
            "AMS Neve 33609/N",
            1970,
            DynamicsTopology::diodeBridge,
            10000.0,
            80.0,
            FrequencyBandSpec {20.0, 20000.0},
            2.0,
            3.0,
            50.0,
            1500.0,
            0.45},
    }};
}

[[nodiscard]] inline double rmsVoltageToDbu(double voltageRms) noexcept
{
    if (!(voltageRms > 0.0)) {
        return -300.0;
    }
    return 20.0 * std::log10(voltageRms / 0.775);
}

[[nodiscard]] inline double dbuToRmsVoltage(double dbu) noexcept
{
    return 0.775 * std::pow(10.0, dbu / 20.0);
}

[[nodiscard]] inline double noiseVoltageFromDistance(double nominalVoltageRms,
                                                     double noiseDistanceDb) noexcept
{
    if (!(nominalVoltageRms > 0.0)) {
        return 0.0;
    }
    return nominalVoltageRms * std::pow(10.0, -noiseDistanceDb / 20.0);
}

[[nodiscard]] inline double fourPointFundamentalAmplitude(const FourPointDistortionCurrents& currents) noexcept
{
    return ((currents.i3 + currents.i4) - (currents.i1 + currents.i2)) / 3.0;
}

[[nodiscard]] inline double fourPointThirdHarmonicAmplitude(const FourPointDistortionCurrents& currents) noexcept
{
    return ((currents.i3 - currents.i2) / 3.0) - ((currents.i4 - currents.i1) / 6.0);
}

[[nodiscard]] inline double fourPointDistortionPercent(const FourPointDistortionCurrents& currents) noexcept
{
    const auto fundamental = fourPointFundamentalAmplitude(currents);
    if (fundamental == 0.0) {
        return 0.0;
    }
    return std::fabs(fourPointThirdHarmonicAmplitude(currents) / fundamental) * 100.0;
}

} // namespace u273::reference
