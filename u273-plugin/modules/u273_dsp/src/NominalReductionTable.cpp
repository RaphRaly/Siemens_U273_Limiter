#include "u273/dsp/NominalReductionTable.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace u273::dsp {

namespace {

constexpr int kCommandPoints = 65;
constexpr float kMinCommandVolt = 0.0f;
constexpr float kMaxCommandVolt = 3.0f;
constexpr float kDiodeA = 308.0f;
constexpr float kDiodeB = 0.16f;
constexpr float kBridgeFeedResistanceOhm = 5600.0f;
constexpr float kCurrentMinMicroAmp = 2.0f;
constexpr float kCurrentMaxMicroAmp = 500.0f;
constexpr float kResistanceCoefficientOhm = 48300.0f;
constexpr float kResistanceExponent = -0.84f;
constexpr float kMaxGainReductionDb = 60.0f;

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] float bridgeCurrentMicroAmp(float commandVolt) noexcept
{
    if (!finite(commandVolt) || commandVolt <= 0.0f) {
        return 0.0f;
    }

    const auto commandMilliVolt = commandVolt * 1000.0f;
    const auto ratio = commandMilliVolt / kDiodeA;
    if (!finite(ratio) || ratio <= 0.0f) {
        return 0.0f;
    }

    const auto current = std::pow(ratio, 1.0f / kDiodeB);
    if (!finite(current)) {
        return kCurrentMaxMicroAmp;
    }
    return std::clamp(current, 0.0f, kCurrentMaxMicroAmp);
}

[[nodiscard]] float diodeDynamicResistanceOhm(float currentMicroAmp) noexcept
{
    if (!finite(currentMicroAmp) || currentMicroAmp < kCurrentMinMicroAmp) {
        return 1.0e12f;
    }
    const auto boundedCurrent = std::clamp(currentMicroAmp, kCurrentMinMicroAmp, kCurrentMaxMicroAmp);
    const auto resistance = kResistanceCoefficientOhm * std::pow(boundedCurrent, kResistanceExponent);
    return finite(resistance) && resistance > 0.0f ? resistance : 1.0e12f;
}

[[nodiscard]] float gainReductionDbForCommand(float commandVolt) noexcept
{
    const auto current = bridgeCurrentMicroAmp(commandVolt);
    const auto resistance = diodeDynamicResistanceOhm(current);
    const auto gain = resistance / (resistance + kBridgeFeedResistanceOhm);
    const auto reduction = -20.0f * std::log10(std::clamp(gain, 1.0e-6f, 1.0f));
    if (!finite(reduction)) {
        return 0.0f;
    }
    return std::clamp(reduction, 0.0f, kMaxGainReductionDb);
}

void computeDerivatives(std::vector<TableReductionPoint>& points)
{
    if (points.size() < 2) {
        return;
    }

    for (std::size_t index = 0; index < points.size(); ++index) {
        std::size_t left = index == 0 ? 0 : index - 1;
        std::size_t right = index + 1 < points.size() ? index + 1 : index;
        if (left == right && right + 1 < points.size()) {
            ++right;
        }

        const auto dx = points[right].commandVolt - points[left].commandVolt;
        points[index].dGainReductionDbDCommand = dx > 0.0f && finite(dx)
            ? (points[right].gainReductionDb - points[left].gainReductionDb) / dx
            : std::numeric_limits<float>::quiet_NaN();
    }
}

} // namespace

std::vector<TableReductionPoint> makeNominalU273ReductionTable()
{
    std::vector<TableReductionPoint> points {};
    points.reserve(kCommandPoints);
    const auto denominator = static_cast<float>(kCommandPoints - 1);
    for (int index = 0; index < kCommandPoints; ++index) {
        const auto fraction = static_cast<float>(index) / denominator;
        const auto command = kMinCommandVolt + fraction * (kMaxCommandVolt - kMinCommandVolt);
        points.push_back(TableReductionPoint {command, gainReductionDbForCommand(command), 0.0f});
    }
    computeDerivatives(points);
    return points;
}

} // namespace u273::dsp
