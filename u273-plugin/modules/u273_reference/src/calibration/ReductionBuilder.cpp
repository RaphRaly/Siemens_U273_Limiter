#include "u273/reference/calibration/ReductionBuilder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>

namespace u273::reference::calibration {

namespace {

constexpr double kBridgeFeedResistanceOhm = 5600.0;
constexpr double kCurrentMinMicroAmp = 2.0;
constexpr double kCurrentMaxMicroAmp = 500.0;
constexpr double kResistanceCoefficientOhm = 48300.0;
constexpr double kResistanceExponent = -0.84;
constexpr double kMaxGainReductionDb = 60.0;

[[nodiscard]] bool finite(double value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] double bridgeCurrentMicroAmp(double commandVolt,
                                           const ActiveModelParameters& parameters) noexcept
{
    if (!finite(commandVolt) || commandVolt <= 0.0) {
        return 0.0;
    }

    const auto commandMilliVolt = commandVolt * 1000.0;
    const auto coefficient = std::max(parameters.diodeA.value, 1.0e-12);
    const auto exponent = std::max(parameters.diodeB.value, 1.0e-12);
    const auto ratio = commandMilliVolt / coefficient;
    if (!finite(ratio) || ratio <= 0.0) {
        return 0.0;
    }

    const auto current = std::pow(ratio, 1.0 / exponent);
    if (!finite(current)) {
        return kCurrentMaxMicroAmp;
    }
    return std::clamp(current, 0.0, kCurrentMaxMicroAmp);
}

[[nodiscard]] double diodeDynamicResistanceOhm(double currentMicroAmp) noexcept
{
    if (!finite(currentMicroAmp) || currentMicroAmp < kCurrentMinMicroAmp) {
        return 1.0e12;
    }
    const auto boundedCurrent = std::clamp(currentMicroAmp, kCurrentMinMicroAmp, kCurrentMaxMicroAmp);
    const auto resistance = kResistanceCoefficientOhm * std::pow(boundedCurrent, kResistanceExponent);
    return finite(resistance) && resistance > 0.0 ? resistance : 1.0e12;
}

[[nodiscard]] double gainReductionDbForCommand(double commandVolt,
                                               const ActiveModelParameters& parameters) noexcept
{
    const auto current = bridgeCurrentMicroAmp(commandVolt, parameters);
    const auto resistance = diodeDynamicResistanceOhm(current);
    const auto gain = resistance / (resistance + kBridgeFeedResistanceOhm);
    const auto reduction = -20.0 * std::log10(std::clamp(gain, 1.0e-6, 1.0));
    if (!finite(reduction)) {
        return 0.0;
    }
    return std::clamp(reduction, 0.0, kMaxGainReductionDb);
}

[[nodiscard]] bool optionsAreValid(const ReductionBuildOptions& options) noexcept
{
    return finite(options.sampleRate)
        && options.sampleRate > 0.0
        && finite(options.minCommandVolt)
        && finite(options.maxCommandVolt)
        && options.maxCommandVolt > options.minCommandVolt
        && options.commandPoints >= 2
        && finite(options.monotonicToleranceDb)
        && options.monotonicToleranceDb >= 0.0
        && finite(options.derivativeLimitDbPerVolt)
        && options.derivativeLimitDbPerVolt > 0.0;
}

void computeDerivatives(std::vector<ReductionTablePoint>& points)
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
        if (dx > 0.0 && finite(dx)) {
            points[index].dGainReductionDbDCommand =
                (points[right].gainReductionDb - points[left].gainReductionDb) / dx;
        } else {
            points[index].dGainReductionDbDCommand = std::numeric_limits<double>::quiet_NaN();
        }
    }
}

void validateShape(ReductionBuildResult& result, const ReductionBuildOptions& options)
{
    result.monotonic = true;
    result.smoothC0 = true;
    result.smoothC1 = true;

    for (std::size_t index = 0; index < result.points.size(); ++index) {
        const auto& point = result.points[index];
        if (!finite(point.commandVolt) || !finite(point.gainReductionDb)) {
            result.smoothC0 = false;
            result.failures.push_back("reduction table contains a non-finite point");
        }
        if (!finite(point.dGainReductionDbDCommand)
            || std::abs(point.dGainReductionDbDCommand) > options.derivativeLimitDbPerVolt) {
            result.smoothC1 = false;
            result.failures.push_back("reduction table derivative is non-finite or exceeds limit");
        }

        if (index > 0) {
            const auto previous = result.points[index - 1].gainReductionDb;
            if (point.gainReductionDb + options.monotonicToleranceDb < previous) {
                result.monotonic = false;
                result.failures.push_back("reduction table is not monotone non-decreasing");
            }
        }
    }

    std::sort(result.failures.begin(), result.failures.end());
    result.failures.erase(std::unique(result.failures.begin(), result.failures.end()), result.failures.end());
}

} // namespace

ReductionBuildResult ReductionBuilder::build(
    const ActiveModelParameters& parameters,
    const u273::reference::state_space::CircuitGraph& referenceCircuit,
    const ReductionBuildOptions& options) const
{
    ReductionBuildResult result {};
    result.validInput = parameters.isValid()
        && optionsAreValid(options)
        && referenceCircuit.nodeCount() > 1
        && !referenceCircuit.diodes().empty();

    if (!parameters.isValid()) {
        result.failures.push_back("invalid calibrated active-model parameters");
    }
    if (!optionsAreValid(options)) {
        result.failures.push_back("invalid reduction build options");
    }
    if (referenceCircuit.nodeCount() <= 1 || referenceCircuit.diodes().empty()) {
        result.failures.push_back("reference circuit must contain nodes and diode bridge devices");
    }
    if (!result.validInput) {
        return result;
    }

    result.points.reserve(static_cast<std::size_t>(options.commandPoints));
    const auto denominator = static_cast<double>(options.commandPoints - 1);
    for (int index = 0; index < options.commandPoints; ++index) {
        const auto fraction = static_cast<double>(index) / denominator;
        const auto command = options.minCommandVolt
            + fraction * (options.maxCommandVolt - options.minCommandVolt);
        result.points.push_back(ReductionTablePoint {
            command,
            gainReductionDbForCommand(command, parameters),
            0.0});
    }

    computeDerivatives(result.points);
    validateShape(result, options);
    result.built = result.validInput && result.monotonic && result.smoothC0 && result.smoothC1;
    return result;
}

bool writeReductionTableCsv(const ReductionBuildResult& result, const std::filesystem::path& path)
{
    if (result.points.empty()) {
        return false;
    }

    std::ofstream out {path};
    if (!out) {
        return false;
    }

    out << "commandVolt,gainReductionDb,dGainReductionDbDCommand\n";
    out << std::scientific << std::setprecision(12);
    for (const auto& point : result.points) {
        out << point.commandVolt << ','
            << point.gainReductionDb << ','
            << point.dGainReductionDbDCommand << '\n';
    }
    return static_cast<bool>(out);
}

bool writeReductionTableJson(const ReductionBuildResult& result, const std::filesystem::path& path)
{
    if (result.points.empty()) {
        return false;
    }

    std::ofstream out {path};
    if (!out) {
        return false;
    }

    out << "{\n";
    out << "  \"validInput\": " << (result.validInput ? "true" : "false") << ",\n";
    out << "  \"built\": " << (result.built ? "true" : "false") << ",\n";
    out << "  \"monotonic\": " << (result.monotonic ? "true" : "false") << ",\n";
    out << "  \"smoothC0\": " << (result.smoothC0 ? "true" : "false") << ",\n";
    out << "  \"smoothC1\": " << (result.smoothC1 ? "true" : "false") << ",\n";
    out << "  \"points\": [\n";
    out << std::scientific << std::setprecision(12);
    for (std::size_t index = 0; index < result.points.size(); ++index) {
        const auto& point = result.points[index];
        out << "    {\"commandVolt\": " << point.commandVolt
            << ", \"gainReductionDb\": " << point.gainReductionDb
            << ", \"dGainReductionDbDCommand\": " << point.dGainReductionDbDCommand
            << "}";
        out << (index + 1 < result.points.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

} // namespace u273::reference::calibration
