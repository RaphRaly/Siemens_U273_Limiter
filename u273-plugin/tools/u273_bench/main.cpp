// u273_bench - scientific demo CLI for the U273 plugin.
//
// Loads the same golden CalibrationDataset that the offline runner consumes,
// runs B6B11CalibrationRunner::runOffline with all gates enabled, then drives
// the calibrated full-active model with three synthetic signals (sine, kick,
// noise) and writes per-signal CSVs plus a Markdown report.
//
// The bench must never crash: all failures funnel into SCIENTIFIC_REPORT.md.
// Exit codes: 0 on success (even when calibration fails), 1 only on malformed
// argv. PowerShell 5.1 friendly.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "u273/core/ProcessContext.h"
#include "u273/dsp/U273DspEngine.h"
#include "u273/reference/calibration/ActiveModelParameterMapping.h"
#include "u273/reference/calibration/ActiveModelParameters.h"
#include "u273/reference/calibration/B6B11CalibrationRunner.h"
#include "u273/reference/calibration/BoundedCalibrationSolver.h"
#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/CalibrationReport.h"
#include "u273/reference/calibration/ReductionBuilder.h"
#include "u273/reference/calibration/ThdBench.h"
#include "u273/reference/state_space/CircuitGraph.h"
#include "u273/reference/state_space/StateSpaceSolver.h"
#include "u273/reference/state_space/U273NetlistLoader.h"

namespace fs = std::filesystem;
namespace calib = u273::reference::calibration;
namespace ss = u273::reference::state_space;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kWarmupSamples = 256;

#ifndef U273_DEFAULT_DATASET_DIR
#define U273_DEFAULT_DATASET_DIR "results"
#endif

// Apply calibrated active-model parameters through the same shared mapping
// used by the calibration solver and audio gate.
[[nodiscard]] ss::CircuitGraph applyParameters(ss::CircuitGraph circuit,
                                                const calib::ActiveModelParameters& parameters)
{
    return calib::applyActiveModelParametersToCircuit(std::move(circuit), parameters);
}

[[nodiscard]] ss::NodeId sameNode(const ss::CircuitGraph& source,
                                  ss::CircuitGraph& target,
                                  ss::NodeId node)
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

[[nodiscard]] ss::CircuitGraph cloneWithCommandSourceVoltage(const ss::CircuitGraph& source,
                                                              const std::string& commandSourceId,
                                                              double voltage)
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
                                currentSource.currentAmp);
    }
    for (const auto& sourceVoltage : source.voltageSources()) {
        const auto replacement = sourceVoltage.id == commandSourceId
            ? voltage
            : sourceVoltage.voltage;
        target.addVoltageSource(sourceVoltage.id,
                                sameNode(source, target, sourceVoltage.positive),
                                sameNode(source, target, sourceVoltage.negative),
                                replacement);
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
    return target;
}

[[nodiscard]] std::string preferredCommandSourceId(const ss::CircuitGraph& circuit)
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id.find("B11") != std::string::npos) {
            return source.id;
        }
    }
    return circuit.voltageSources().empty() ? std::string {} : circuit.voltageSources().front().id;
}

[[nodiscard]] double dcBiasFor(const ss::CircuitGraph& circuit, const std::string& sourceId)
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id == sourceId) {
            return source.voltage;
        }
    }
    return 0.0;
}

struct SignalMetrics {
    std::string name {};
    double peakInLin {};
    double peakOutLin {};
    double peakInDb {};
    double peakOutDb {};
    double rmsInLin {};
    double rmsOutLin {};
    double rmsInDb {};
    double rmsOutDb {};
    double thdDb {std::numeric_limits<double>::quiet_NaN()};
    double avgGrDb {};
    bool drovenOk {};
    std::vector<std::string> failures {};
};

struct RealtimeDspSignalMetrics {
    std::string name {};
    std::string description {};
    double durationSeconds {};
    double sampleRate {};
    double peakInDb {};
    double peakOutDb {};
    double rmsInDb {};
    double rmsOutDb {};
    double rmsGainDb {};
    double maxMeterGrDb {};
    double avgMeterGrDb {};
    double thdDb {std::numeric_limits<double>::quiet_NaN()};
    bool processedOk {};
    bool clipped {};
    bool usingReductionTable {};
    std::string boundary {};
    std::string inputWav {};
    std::string outputWav {};
    std::string inputCsv {};
    std::string outputCsv {};
    std::vector<std::string> failures {};
};

[[nodiscard]] double linToDb(double linear)
{
    if (!std::isfinite(linear) || linear <= 0.0) {
        return -200.0;
    }
    return 20.0 * std::log10(linear);
}

[[nodiscard]] double computePeak(const std::vector<double>& samples)
{
    double peak = 0.0;
    for (const auto& s : samples) {
        const auto a = std::fabs(s);
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

[[nodiscard]] double computeRms(const std::vector<double>& samples)
{
    if (samples.empty()) {
        return 0.0;
    }
    long double accum = 0.0L;
    for (const auto& s : samples) {
        accum += static_cast<long double>(s) * static_cast<long double>(s);
    }
    return std::sqrt(static_cast<double>(accum / static_cast<long double>(samples.size())));
}

[[nodiscard]] double computePeakFloat(const std::vector<float>& samples)
{
    double peak = 0.0;
    for (const auto s : samples) {
        const auto a = std::fabs(static_cast<double>(s));
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

[[nodiscard]] double computeRmsFloat(const std::vector<float>& samples)
{
    if (samples.empty()) {
        return 0.0;
    }
    long double accum = 0.0L;
    for (const auto s : samples) {
        const auto value = static_cast<long double>(s);
        accum += value * value;
    }
    return std::sqrt(static_cast<double>(accum / static_cast<long double>(samples.size())));
}

[[nodiscard]] std::vector<float> trimFront(const std::vector<float>& samples, int trimSamples)
{
    if (trimSamples <= 0 || trimSamples >= static_cast<int>(samples.size())) {
        return samples;
    }
    return std::vector<float> {samples.begin() + trimSamples, samples.end()};
}

// Synthesize a 1 kHz sine at -6 dBFS over `count` samples at `sampleRate`.
[[nodiscard]] std::vector<double> synthesizeSine(int count, double sampleRate, double frequencyHz, double amplitudeDbfs)
{
    std::vector<double> output(static_cast<std::size_t>(count));
    const auto amplitude = std::pow(10.0, amplitudeDbfs / 20.0);
    for (int i = 0; i < count; ++i) {
        const auto phase = 2.0 * kPi * frequencyHz * static_cast<double>(i) / sampleRate;
        output[static_cast<std::size_t>(i)] = amplitude * std::sin(phase);
    }
    return output;
}

// Kick: 60 Hz sine windowed by exp(-t/30ms), full amplitude.
[[nodiscard]] std::vector<double> synthesizeKick(int count, double sampleRate)
{
    std::vector<double> output(static_cast<std::size_t>(count));
    const auto tauSeconds = 0.030;
    const auto freqHz = 60.0;
    for (int i = 0; i < count; ++i) {
        const auto t = static_cast<double>(i) / sampleRate;
        const auto envelope = std::exp(-t / tauSeconds);
        const auto carrier = std::sin(2.0 * kPi * freqHz * t);
        output[static_cast<std::size_t>(i)] = envelope * carrier;
    }
    return output;
}

// White noise burst, uniform in [-0.5, 0.5], deterministic seed.
[[nodiscard]] std::vector<double> synthesizeNoise(int count)
{
    std::vector<double> output(static_cast<std::size_t>(count));
    std::mt19937_64 rng {0xC0FFEE2026ULL};
    std::uniform_real_distribution<double> dist {-0.5, 0.5};
    for (int i = 0; i < count; ++i) {
        output[static_cast<std::size_t>(i)] = dist(rng);
    }
    return output;
}

[[nodiscard]] std::vector<float> synthesizeSineFloat(int count,
                                                     double sampleRate,
                                                     double frequencyHz,
                                                     double amplitudeDbfs)
{
    std::vector<float> output(static_cast<std::size_t>(count));
    const auto amplitude = std::pow(10.0, amplitudeDbfs / 20.0);
    for (int i = 0; i < count; ++i) {
        const auto phase = 2.0 * kPi * frequencyHz * static_cast<double>(i) / sampleRate;
        output[static_cast<std::size_t>(i)] = static_cast<float>(amplitude * std::sin(phase));
    }
    return output;
}

[[nodiscard]] std::vector<float> synthesizeLogSweepFloat(int count,
                                                         double sampleRate,
                                                         double startFrequencyHz,
                                                         double endFrequencyHz,
                                                         double amplitudeDbfs)
{
    std::vector<float> output(static_cast<std::size_t>(count));
    const auto durationSeconds = static_cast<double>(count) / sampleRate;
    const auto amplitude = std::pow(10.0, amplitudeDbfs / 20.0);
    const auto ratio = std::max(endFrequencyHz / startFrequencyHz, 1.0001);
    const auto logRatio = std::log(ratio);
    const auto phaseScale = 2.0 * kPi * startFrequencyHz * durationSeconds / logRatio;
    for (int i = 0; i < count; ++i) {
        const auto tNorm = static_cast<double>(i) / static_cast<double>(std::max(1, count - 1));
        const auto phase = phaseScale * (std::exp(logRatio * tNorm) - 1.0);
        output[static_cast<std::size_t>(i)] = static_cast<float>(amplitude * std::sin(phase));
    }
    return output;
}

// Drive the calibrated reference circuit with an arbitrary input signal,
// reusing the ThdBench transient driver pattern. Returns the captured CMD
// samples (DC-removed). On the first non-converged step the bench bails out
// and reports a failure but never throws.
struct DriveResult {
    std::vector<double> capturedOutput {};
    bool fullyConverged {};
    std::vector<std::string> failures {};
};

[[nodiscard]] DriveResult driveSignalThroughModel(const ss::CircuitGraph& calibratedCircuit,
                                                   const std::string& commandSourceId,
                                                   double dcBias,
                                                   const std::vector<double>& input,
                                                   double sampleRate)
{
    DriveResult result {};
    result.capturedOutput.reserve(input.size());

    const auto outputNode = calibratedCircuit.findNode("CMD");
    if (outputNode.value <= 0 || outputNode.value >= calibratedCircuit.nodeCount()) {
        result.failures.push_back("CMD output node not found in calibrated circuit");
        return result;
    }
    const auto outputIndex = static_cast<std::size_t>(outputNode.value - 1);

    ss::SolverOptions solverOptions {};
    solverOptions.sampleRate = sampleRate;
    solverOptions.method = ss::IntegrationMethod::trBdf2;
    if (!solverOptions.isValid()) {
        result.failures.push_back("solver options invalid for signal drive");
        return result;
    }

    ss::CircuitState state {};
    {
        ss::StateSpaceSolver stateFactory {calibratedCircuit};
        state = stateFactory.createInitialState();
    }
    if (!state.isValidFor(calibratedCircuit)) {
        result.failures.push_back("initial state inconsistent with calibrated circuit");
        return result;
    }

    int convergedSamples = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto drive = dcBias + input[i];
        const auto stepCircuit = cloneWithCommandSourceVoltage(calibratedCircuit,
                                                                commandSourceId,
                                                                drive);
        ss::StateSpaceSolver solver {stepCircuit};
        const auto step = solver.step(state, solverOptions);
        if (!step.validInput || !step.converged) {
            result.failures.push_back("solver step " + std::to_string(i) + " did not converge");
            // Fill the remaining samples with NaN so the CSV still has the
            // right length, and stop driving; never throw.
            while (result.capturedOutput.size() < input.size()) {
                result.capturedOutput.push_back(std::numeric_limits<double>::quiet_NaN());
            }
            return result;
        }
        ++convergedSamples;

        double sample = 0.0;
        if (outputIndex < state.unknowns.size()) {
            sample = state.unknowns[outputIndex] - dcBias;
        }
        if (!std::isfinite(sample)) {
            result.failures.push_back("non-finite CMD sample at index " + std::to_string(i));
            sample = std::numeric_limits<double>::quiet_NaN();
        }
        result.capturedOutput.push_back(sample);
    }

    result.fullyConverged = convergedSamples == static_cast<int>(input.size());
    return result;
}

void writeCsv(const fs::path& path, const std::vector<double>& samples)
{
    std::ofstream out {path};
    if (!out) {
        return;
    }
    out << "sample_index,voltage\n";
    out << std::scientific << std::setprecision(9);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        out << i << "," << samples[i] << "\n";
    }
}

void writeCsv(const fs::path& path, const std::vector<float>& samples)
{
    std::ofstream out {path};
    if (!out) {
        return;
    }
    out << "sample_index,amplitude\n";
    out << std::scientific << std::setprecision(9);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        out << i << "," << samples[i] << "\n";
    }
}

void writeLittleEndian16(std::ofstream& out, std::uint16_t value)
{
    out.put(static_cast<char>(value & 0xffU));
    out.put(static_cast<char>((value >> 8U) & 0xffU));
}

void writeLittleEndian32(std::ofstream& out, std::uint32_t value)
{
    out.put(static_cast<char>(value & 0xffU));
    out.put(static_cast<char>((value >> 8U) & 0xffU));
    out.put(static_cast<char>((value >> 16U) & 0xffU));
    out.put(static_cast<char>((value >> 24U) & 0xffU));
}

void writeWavFloatMono(const fs::path& path, const std::vector<float>& samples, double sampleRate)
{
    std::ofstream out {path, std::ios::binary};
    if (!out) {
        return;
    }

    const auto sampleRateInt = static_cast<std::uint32_t>(std::llround(sampleRate));
    constexpr std::uint16_t kChannels = 1;
    constexpr std::uint16_t kBitsPerSample = 32;
    constexpr std::uint16_t kBytesPerSample = kBitsPerSample / 8;
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * kBytesPerSample);
    const auto riffBytes = 36U + dataBytes;
    const auto byteRate = sampleRateInt * kChannels * kBytesPerSample;
    const auto blockAlign = static_cast<std::uint16_t>(kChannels * kBytesPerSample);

    out.write("RIFF", 4);
    writeLittleEndian32(out, riffBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLittleEndian32(out, 16);
    writeLittleEndian16(out, 3);
    writeLittleEndian16(out, kChannels);
    writeLittleEndian32(out, sampleRateInt);
    writeLittleEndian32(out, byteRate);
    writeLittleEndian16(out, blockAlign);
    writeLittleEndian16(out, kBitsPerSample);
    out.write("data", 4);
    writeLittleEndian32(out, dataBytes);
    for (const auto sample : samples) {
        out.write(reinterpret_cast<const char*>(&sample), sizeof(float));
    }
}

[[nodiscard]] std::string formatDouble(double value, int precision = 6)
{
    if (!std::isfinite(value)) {
        return "n/a";
    }
    std::ostringstream s;
    s << std::fixed << std::setprecision(precision) << value;
    return s.str();
}

[[nodiscard]] std::string boolStr(bool value)
{
    return value ? "true" : "false";
}

[[nodiscard]] std::string timestampNow()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream s;
    s << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return s.str();
}

void computeMetrics(SignalMetrics& metrics,
                    const std::vector<double>& input,
                    const std::vector<double>& output)
{
    metrics.peakInLin = computePeak(input);
    metrics.peakOutLin = computePeak(output);
    metrics.rmsInLin = computeRms(input);
    metrics.rmsOutLin = computeRms(output);
    metrics.peakInDb = linToDb(metrics.peakInLin);
    metrics.peakOutDb = linToDb(metrics.peakOutLin);
    metrics.rmsInDb = linToDb(metrics.rmsInLin);
    metrics.rmsOutDb = linToDb(metrics.rmsOutLin);
    if (metrics.rmsInLin > 0.0 && metrics.rmsOutLin > 0.0) {
        metrics.avgGrDb = 20.0 * std::log10(metrics.rmsOutLin / metrics.rmsInLin);
    } else {
        metrics.avgGrDb = std::numeric_limits<double>::quiet_NaN();
    }
}

[[nodiscard]] double dftMagnitudeFloat(const std::vector<float>& samples, int bin)
{
    std::complex<double> sum {};
    const auto size = static_cast<double>(samples.size());
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto phase = -2.0 * kPi * static_cast<double>(bin)
            * static_cast<double>(index) / size;
        sum += static_cast<double>(samples[index])
            * std::complex<double> {std::cos(phase), std::sin(phase)};
    }
    return samples.empty() ? 0.0 : 2.0 * std::abs(sum) / size;
}

[[nodiscard]] double computeSineThdDb(const std::vector<float>& samples,
                                      double sampleRate,
                                      double frequencyHz,
                                      int highestHarmonic)
{
    if (samples.empty() || !(sampleRate > 0.0) || !(frequencyHz > 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto exactBin = frequencyHz * static_cast<double>(samples.size()) / sampleRate;
    const auto fundamentalBin = static_cast<int>(std::llround(exactBin));
    if (fundamentalBin <= 0 || std::fabs(exactBin - static_cast<double>(fundamentalBin)) > 1.0e-6) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const auto fundamental = dftMagnitudeFloat(samples, fundamentalBin);
    if (!(fundamental > 0.0) || !std::isfinite(fundamental)) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double harmonicPower = 0.0;
    for (int harmonic = 2; harmonic <= highestHarmonic; ++harmonic) {
        const auto bin = fundamentalBin * harmonic;
        if (bin >= static_cast<int>(samples.size() / 2)) {
            break;
        }
        const auto magnitude = dftMagnitudeFloat(samples, bin);
        harmonicPower += magnitude * magnitude;
    }

    const auto thd = std::sqrt(harmonicPower) / fundamental;
    if (!(thd > 0.0) || !std::isfinite(thd)) {
        return -120.0;
    }
    return 20.0 * std::log10(thd);
}

struct RealtimeDspRenderResult {
    std::vector<float> output {};
    bool processedOk {};
    bool clipped {};
    bool usingReductionTable {};
    std::string boundary {};
    double maxMeterGrDb {};
    double avgMeterGrDb {};
    std::vector<std::string> failures {};
};

[[nodiscard]] std::vector<u273::dsp::TableReductionPoint> makeDspTable(
    const std::vector<calib::ReductionTablePoint>& source)
{
    std::vector<u273::dsp::TableReductionPoint> table {};
    table.reserve(source.size());
    for (const auto& point : source) {
        table.push_back(u273::dsp::TableReductionPoint {
            static_cast<float>(point.commandVolt),
            static_cast<float>(point.gainReductionDb),
            static_cast<float>(point.dGainReductionDbDCommand)});
    }
    return table;
}

[[nodiscard]] RealtimeDspRenderResult renderRealtimeDspSignal(
    const std::vector<float>& input,
    double sampleRate,
    int blockSize,
    const u273::core::ParameterSnapshot& snapshot,
    const std::vector<u273::dsp::TableReductionPoint>* reductionTable)
{
    RealtimeDspRenderResult result {};
    result.output = input;

    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {
        sampleRate,
        blockSize,
        1,
        u273::dsp::RealtimeQualityMode::precise});
    if (!engine.isPrepared()) {
        result.failures.push_back("U273DspEngine did not prepare");
        return result;
    }
    if (!snapshot.isValid()) {
        result.failures.push_back("realtime DSP snapshot is invalid");
        return result;
    }
    if (reductionTable != nullptr) {
        if (!engine.loadReductionTable(*reductionTable)) {
            result.failures.push_back("U273DspEngine rejected the reduction table");
            return result;
        }
        result.usingReductionTable = engine.isUsingTableReduction();
    }
    result.boundary = u273::core::toString(engine.boundary());

    auto meterCount = 0;
    double meterSum = 0.0;
    for (std::size_t offset = 0; offset < result.output.size(); offset += static_cast<std::size_t>(blockSize)) {
        const auto remaining = result.output.size() - offset;
        const auto currentBlock = static_cast<int>(
            std::min<std::size_t>(remaining, static_cast<std::size_t>(blockSize)));
        float* channel = result.output.data() + offset;
        std::array<float*, 1> channels {channel};
        u273::core::ProcessContext context {
            u273::core::AudioBlockView {channels.data(), 1, currentBlock},
            sampleRate,
            static_cast<std::uint64_t>(meterCount),
            true};

        u273::core::MeterFrame meter {};
        const auto status = engine.process(context, snapshot, &meter);
        if (status != u273::dsp::ProcessStatus::ok) {
            result.failures.push_back("U273DspEngine process failed at block " + std::to_string(meterCount));
            return result;
        }
        if (!meter.isValid()) {
            result.failures.push_back("U273DspEngine meter invalid at block " + std::to_string(meterCount));
            return result;
        }
        result.clipped = result.clipped || meter.clipFlag;
        result.maxMeterGrDb = std::max(result.maxMeterGrDb, static_cast<double>(meter.gainReductionDb));
        meterSum += static_cast<double>(meter.gainReductionDb);
        ++meterCount;
    }

    result.processedOk = result.failures.empty();
    result.avgMeterGrDb = meterCount > 0 ? meterSum / static_cast<double>(meterCount) : 0.0;
    return result;
}

void fillRealtimeDspMetrics(RealtimeDspSignalMetrics& metrics,
                            const std::vector<float>& input,
                            const std::vector<float>& output)
{
    const auto inputPeak = computePeakFloat(input);
    const auto outputPeak = computePeakFloat(output);
    const auto inputRms = computeRmsFloat(input);
    const auto outputRms = computeRmsFloat(output);

    metrics.peakInDb = linToDb(inputPeak);
    metrics.peakOutDb = linToDb(outputPeak);
    metrics.rmsInDb = linToDb(inputRms);
    metrics.rmsOutDb = linToDb(outputRms);
    metrics.rmsGainDb = (inputRms > 0.0 && outputRms > 0.0)
        ? 20.0 * std::log10(outputRms / inputRms)
        : std::numeric_limits<double>::quiet_NaN();
}

void writeRealtimeDspMetricsCsv(const fs::path& path,
                                const std::vector<RealtimeDspSignalMetrics>& metrics)
{
    std::ofstream out {path};
    if (!out) {
        return;
    }
    out << "signal,duration_s,sample_rate,peak_in_db,peak_out_db,rms_in_db,rms_out_db,rms_gain_db,"
           "max_meter_gr_db,avg_meter_gr_db,thd_db,processed_ok,clipped,using_reduction_table,boundary,input_wav,output_wav\n";
    for (const auto& m : metrics) {
        out << m.name
            << "," << formatDouble(m.durationSeconds, 6)
            << "," << formatDouble(m.sampleRate, 2)
            << "," << formatDouble(m.peakInDb, 3)
            << "," << formatDouble(m.peakOutDb, 3)
            << "," << formatDouble(m.rmsInDb, 3)
            << "," << formatDouble(m.rmsOutDb, 3)
            << "," << formatDouble(m.rmsGainDb, 3)
            << "," << formatDouble(m.maxMeterGrDb, 3)
            << "," << formatDouble(m.avgMeterGrDb, 3)
            << "," << formatDouble(m.thdDb, 3)
            << "," << boolStr(m.processedOk)
            << "," << boolStr(m.clipped)
            << "," << boolStr(m.usingReductionTable)
            << "," << m.boundary
            << "," << m.inputWav
            << "," << m.outputWav
            << "\n";
    }
}

enum class AliasingSignalKind {
    sine,
    sweep,
    multitone,
    burstStep
};

struct AliasingSignalSpec {
    std::string name {};
    AliasingSignalKind kind {};
    double frequencyHz {};
    bool measureThd {};
};

struct AliasingBenchMetrics {
    std::string signal {};
    int oversamplingFactor {};
    double sampleRate {};
    double thdDb {std::numeric_limits<double>::quiet_NaN()};
    double aliasEnergyDb {std::numeric_limits<double>::quiet_NaN()};
    double avgGrDb {};
    double maxGrDb {};
    double realtimeVsOfflineDiffDb {std::numeric_limits<double>::quiet_NaN()};
    bool processedOk {};
};

[[nodiscard]] std::vector<float> synthesizeAliasingSignal(const AliasingSignalSpec& spec,
                                                          int count,
                                                          double sampleRate)
{
    std::vector<float> output(static_cast<std::size_t>(count), 0.0f);
    if (count <= 0 || !(sampleRate > 0.0)) {
        return output;
    }

    if (spec.kind == AliasingSignalKind::sine) {
        return synthesizeSineFloat(count, sampleRate, spec.frequencyHz, -6.0);
    }
    if (spec.kind == AliasingSignalKind::sweep) {
        return synthesizeLogSweepFloat(count, sampleRate, 20.0, std::min(20000.0, sampleRate * 0.45), -9.0);
    }
    if (spec.kind == AliasingSignalKind::multitone) {
        const std::array<double, 5> tones {100.0, 700.0, 1900.0, 5100.0, 11300.0};
        for (int i = 0; i < count; ++i) {
            const auto t = static_cast<double>(i) / sampleRate;
            double sample {};
            for (const auto tone : tones) {
                sample += std::sin(2.0 * kPi * tone * t);
            }
            output[static_cast<std::size_t>(i)] = static_cast<float>(0.12 * sample);
        }
        return output;
    }

    for (int i = 0; i < count; ++i) {
        const auto t = static_cast<double>(i) / sampleRate;
        const auto carrier = std::sin(2.0 * kPi * 1000.0 * t);
        const auto envelope = i < count / 4 ? 0.0 : (i < count / 2 ? 0.75 : 0.25);
        output[static_cast<std::size_t>(i)] = static_cast<float>(envelope * carrier);
    }
    return output;
}

[[nodiscard]] std::vector<float> decimateByFactor(const std::vector<float>& input, int factor)
{
    if (factor <= 1) {
        return input;
    }
    std::vector<float> output {};
    output.reserve((input.size() + static_cast<std::size_t>(factor - 1)) / static_cast<std::size_t>(factor));
    for (std::size_t index = 0; index < input.size(); index += static_cast<std::size_t>(factor)) {
        output.push_back(input[index]);
    }
    return output;
}

[[nodiscard]] double rmsDifferenceDb(const std::vector<float>& lhs, const std::vector<float>& rhs)
{
    const auto count = std::min(lhs.size(), rhs.size());
    if (count == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    long double accum {};
    for (std::size_t index = 0; index < count; ++index) {
        const auto delta = static_cast<long double>(lhs[index]) - static_cast<long double>(rhs[index]);
        accum += delta * delta;
    }
    const auto rms = std::sqrt(static_cast<double>(accum / static_cast<long double>(count)));
    return linToDb(rms);
}

[[nodiscard]] std::vector<AliasingBenchMetrics> runAliasingBench(
    double hostSampleRate,
    const u273::core::ParameterSnapshot& snapshot,
    const std::vector<u273::dsp::TableReductionPoint>* reductionTable,
    std::vector<std::string>& failures)
{
    const std::array<int, 4> factors {1, 2, 4, 8};
    const std::array<AliasingSignalSpec, 9> specs {{
        {"sine_100hz", AliasingSignalKind::sine, 100.0, true},
        {"sine_1khz", AliasingSignalKind::sine, 1000.0, true},
        {"sine_5khz", AliasingSignalKind::sine, 5000.0, true},
        {"sine_10khz", AliasingSignalKind::sine, 10000.0, true},
        {"sine_15khz", AliasingSignalKind::sine, 15000.0, true},
        {"sweep_20hz_20khz", AliasingSignalKind::sweep, 0.0, false},
        {"multitone", AliasingSignalKind::multitone, 0.0, false},
        {"burst_step", AliasingSignalKind::burstStep, 0.0, false},
        {"step_1khz", AliasingSignalKind::burstStep, 0.0, false}}};

    std::vector<AliasingBenchMetrics> rows {};
    constexpr double durationSeconds = 0.25;
    constexpr int baseBlockSize = 128;

    for (const auto& spec : specs) {
        std::vector<float> factorOneOutput {};
        for (const auto factor : factors) {
            const auto sampleRate = hostSampleRate * static_cast<double>(factor);
            const auto count = std::max(1, static_cast<int>(std::llround(durationSeconds * sampleRate)));
            const auto input = synthesizeAliasingSignal(spec, count, sampleRate);
            const auto rendered = renderRealtimeDspSignal(input,
                                                          sampleRate,
                                                          baseBlockSize * factor,
                                                          snapshot,
                                                          reductionTable);
            const auto hostOutput = decimateByFactor(rendered.output, factor);
            if (factor == 1) {
                factorOneOutput = hostOutput;
            }

            AliasingBenchMetrics row {};
            row.signal = spec.name;
            row.oversamplingFactor = factor;
            row.sampleRate = sampleRate;
            row.processedOk = rendered.processedOk;
            row.avgGrDb = rendered.avgMeterGrDb;
            row.maxGrDb = rendered.maxMeterGrDb;
            row.realtimeVsOfflineDiffDb = factor == 1
                ? -120.0
                : rmsDifferenceDb(hostOutput, factorOneOutput);
            row.aliasEnergyDb = row.realtimeVsOfflineDiffDb;
            if (spec.measureThd) {
                row.thdDb = computeSineThdDb(hostOutput, hostSampleRate, spec.frequencyHz, 8);
            }
            for (const auto& failure : rendered.failures) {
                failures.push_back("aliasing " + spec.name + " "
                                   + std::to_string(factor) + "x: " + failure);
            }
            rows.push_back(row);
        }
    }

    return rows;
}

void writeAliasingBenchCsv(const fs::path& path, const std::vector<AliasingBenchMetrics>& rows)
{
    std::ofstream out {path};
    if (!out) {
        return;
    }
    out << "signal,oversampling_factor,sample_rate,thd_db,alias_energy_db,avg_gr_db,max_gr_db,"
           "realtime_vs_offline_diff_db,processed_ok\n";
    for (const auto& row : rows) {
        out << row.signal
            << "," << row.oversamplingFactor
            << "," << formatDouble(row.sampleRate, 2)
            << "," << formatDouble(row.thdDb, 3)
            << "," << formatDouble(row.aliasEnergyDb, 3)
            << "," << formatDouble(row.avgGrDb, 3)
            << "," << formatDouble(row.maxGrDb, 3)
            << "," << formatDouble(row.realtimeVsOfflineDiffDb, 3)
            << "," << boolStr(row.processedOk)
            << "\n";
    }
}

void writeReport(const fs::path& reportPath,
                 const std::string& stamp,
                 const calib::CalibrationReport& report,
                 const std::vector<SignalMetrics>& signals,
                 const std::vector<RealtimeDspSignalMetrics>& realtimeDspSignals,
                 const std::vector<AliasingBenchMetrics>& aliasingMetrics,
                 const std::vector<std::string>& benchNotes,
                 const std::vector<std::string>& benchFailures)
{
    std::ofstream out {reportPath};
    if (!out) {
        return;
    }
    out << "# U273 Scientific Demo Bench " << stamp << "\n\n";

    out << "## Calibration outcome\n";
    out << "- Bounded calibration converged: " << boolStr(report.calibrationConverged) << "\n";
    out << "- Validation passed: " << boolStr(report.validationPassed) << "\n";
    out << "- Train cost: " << formatDouble(report.calibrationTrainCost, 6) << "\n";
    out << "- Validation cost: " << formatDouble(report.calibrationValidationCost, 6) << "\n";
    out << "- Iterations: " << report.calibrationIterations << "\n";
    if (report.parametersOnBound.empty()) {
        out << "- Parameters on bound: none\n";
    } else {
        out << "- Parameters on bound: ";
        for (std::size_t i = 0; i < report.parametersOnBound.size(); ++i) {
            if (i > 0) out << ", ";
            out << report.parametersOnBound[i];
        }
        out << "\n";
    }
    out << "\n";

    out << "## Identifiability\n";
    out << "- Condition number: " << formatDouble(report.identifiabilityConditionNumber, 6) << "\n";
    if (report.nonIdentifiableParameters.empty()) {
        out << "- Weak parameters: none\n";
        out << "- Strong correlations: none\n";
    } else {
        out << "- Weak / non-identifiable parameters: ";
        for (std::size_t i = 0; i < report.nonIdentifiableParameters.size(); ++i) {
            if (i > 0) out << ", ";
            out << report.nonIdentifiableParameters[i];
        }
        out << "\n";
    }
    const auto identifiableNames = calib::activeModelParameterNames();
    if (!report.identifiabilitySensitivityNorms.empty()
        && report.identifiabilitySensitivityNorms.size() == identifiableNames.size()) {
        out << "- Sensitivity norms:\n\n";
        out << "| Parameter | Norm | Weak? |\n";
        out << "|---|---:|---|\n";
        for (std::size_t i = 0; i < identifiableNames.size(); ++i) {
            const auto& name = identifiableNames[i];
            const auto weak = std::find(report.weakParameters.begin(),
                                        report.weakParameters.end(),
                                        name) != report.weakParameters.end();
            out << "| " << name
                << " | " << formatDouble(report.identifiabilitySensitivityNorms[i], 6)
                << " | " << boolStr(weak)
                << " |\n";
        }
    }
    out << "- Passed: " << boolStr(report.gates.identifiability) << "\n\n";

    out << "## THD bench\n";
    out << "- Measured THD: " << formatDouble(report.thdBench.measuredThdDb, 3) << " dB\n";
    out << "- Target status: " << report.thdBench.targetStatus << "\n";
    out << "- Target id: " << (report.thdBench.targetId.empty() ? std::string {"n/a"} : report.thdBench.targetId) << "\n";
    out << "- Measurement node: " << report.thdBench.measurementNode << "\n";
    out << "- Mode: " << report.thdBench.mode << "\n";
    out << "- Target THD ceiling: " << formatDouble(report.thdBench.goldenThdDb, 3) << " dB\n";
    out << "- Tolerance: " << formatDouble(report.thdBench.toleranceDb, 3) << " dB\n";
    out << "- Passed: " << boolStr(report.thdBench.passed) << "\n\n";

    out << "## Audio gate\n";
    out << "- **gates.audio = " << boolStr(report.gates.audio) << "**\n";
    out << "- canPromoteBoundary() = " << boolStr(report.canPromoteBoundary()) << "\n\n";

    out << "## Per-signal metrics\n";
    out << "| Signal | Peak in (dB) | Peak out (dB) | RMS in (dB) | RMS out (dB) | THD (dB) | Avg GR (dB) |\n";
    out << "|---|---|---|---|---|---|---|\n";
    for (const auto& m : signals) {
        out << "| " << m.name
            << " | " << formatDouble(m.peakInDb, 2)
            << " | " << formatDouble(m.peakOutDb, 2)
            << " | " << formatDouble(m.rmsInDb, 2)
            << " | " << formatDouble(m.rmsOutDb, 2)
            << " | " << (std::isfinite(m.thdDb) ? formatDouble(m.thdDb, 2) : std::string {"n/a"})
            << " | " << formatDouble(m.avgGrDb, 2)
            << " |\n";
    }
    out << "\n";

    out << "## Realtime DSP audio path\n";
    out << "- Path: U273DspEngine -> DetectorEnvelope -> "
        << ((!realtimeDspSignals.empty() && realtimeDspSignals.front().usingReductionTable)
            ? "TableReductionRealtimeEngine"
            : "AnalogRealtimeEngine")
        << " gain reduction -> wet/output gain\n";
    out << "- Scope: this is the VST/Standalone DSP core, not the offline B6/B11 CMD probe\n";
    out << "- Snapshot: inputGain=0 dB, outputGain=0 dB, drive=1.0, detectorScale=1.0, attack=0.5 ms, release=500 ms, mix=1.0\n";
    out << "| Signal | Boundary | Table | Duration (s) | Peak in (dB) | Peak out (dB) | RMS in (dB) | RMS out (dB) | RMS gain (dB) | Max GR meter (dB) | Avg GR meter (dB) | THD (dB) | Files |\n";
    out << "|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& m : realtimeDspSignals) {
        out << "| " << m.name
            << " | " << m.boundary
            << " | " << boolStr(m.usingReductionTable)
            << " | " << formatDouble(m.durationSeconds, 3)
            << " | " << formatDouble(m.peakInDb, 2)
            << " | " << formatDouble(m.peakOutDb, 2)
            << " | " << formatDouble(m.rmsInDb, 2)
            << " | " << formatDouble(m.rmsOutDb, 2)
            << " | " << formatDouble(m.rmsGainDb, 2)
            << " | " << formatDouble(m.maxMeterGrDb, 2)
            << " | " << formatDouble(m.avgMeterGrDb, 2)
            << " | " << (std::isfinite(m.thdDb) ? formatDouble(m.thdDb, 2) : std::string {"n/a"})
            << " | " << m.outputWav
            << " |\n";
    }
    out << "\n";

    out << "## Aliasing and smoothness bench\n";
    out << "- Oversampling factors: 1x, 2x, 4x, 8x\n";
    out << "- Alias energy is reported as the host-rate RMS difference versus the 1x render; it is a conservative proxy until a true reconstruction filter lands.\n";
    out << "| Signal | OS | THD (dB) | Alias energy (dB) | Avg GR (dB) | Max GR (dB) | RT vs offline diff (dB) | OK |\n";
    out << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& row : aliasingMetrics) {
        out << "| " << row.signal
            << " | " << row.oversamplingFactor << "x"
            << " | " << (std::isfinite(row.thdDb) ? formatDouble(row.thdDb, 2) : std::string {"n/a"})
            << " | " << formatDouble(row.aliasEnergyDb, 2)
            << " | " << formatDouble(row.avgGrDb, 2)
            << " | " << formatDouble(row.maxGrDb, 2)
            << " | " << formatDouble(row.realtimeVsOfflineDiffDb, 2)
            << " | " << boolStr(row.processedOk)
            << " |\n";
    }
    out << "\n";

    out << "## Calibrated parameters\n";
    out << "| Parameter | Value | Lower | Upper | On bound? |\n";
    out << "|---|---|---|---|---|\n";
    const auto& p = report.parameters;
    auto row = [&out](const char* name, const calib::BoundedParameter& bp) {
        out << "| " << name
            << " | " << formatDouble(bp.value, 6)
            << " | " << formatDouble(bp.lower, 6)
            << " | " << formatDouble(bp.upper, 6)
            << " | " << boolStr(bp.touchesBound()) << " |\n";
    };
    row("diodeA", p.diodeA);
    row("diodeB", p.diodeB);
    row("logIs", p.logIs);
    row("betaForward", p.betaForward);
    row("betaReverse", p.betaReverse);
    row("earlyVoltage", p.earlyVoltage);
    row("detectorToCmdScale", p.detectorToCmdScale);
    row("numericalGmin", p.numericalGmin);
    out << "\n";

    out << "## Notes\n";
    if (report.notes.empty() && benchNotes.empty()) {
        out << "- (none)\n";
    } else {
        for (const auto& note : report.notes) {
            out << "- " << note << "\n";
        }
        for (const auto& note : benchNotes) {
            out << "- " << note << "\n";
        }
    }
    out << "\n";

    out << "## Failures\n";
    auto anyFailures =
        !report.calibrationFailures.empty()
        || !report.identifiabilityFailures.empty()
        || !report.rejectedTopologyReasons.empty()
        || !report.thdBench.failures.empty()
        || !benchFailures.empty();
    if (!anyFailures) {
        out << "- (none)\n";
    } else {
        for (const auto& f : report.calibrationFailures) {
            out << "- calibration: " << f << "\n";
        }
        for (const auto& f : report.identifiabilityFailures) {
            out << "- identifiability: " << f << "\n";
        }
        for (const auto& f : report.rejectedTopologyReasons) {
            out << "- rejected topology: " << f << "\n";
        }
        for (const auto& f : report.thdBench.failures) {
            out << "- thd bench: " << f << "\n";
        }
        for (const auto& f : benchFailures) {
            out << "- bench: " << f << "\n";
        }
    }
}

struct CliOptions {
    fs::path outputDir {};
    fs::path datasetDir {};
    double sampleRate {96000.0};
    bool ok {true};
};

[[nodiscard]] CliOptions parseCli(int argc, char** argv)
{
    CliOptions o {};
    o.datasetDir = fs::path {U273_DEFAULT_DATASET_DIR};
    o.outputDir = fs::path {"Demo"} / ("bench_" + timestampNow());
    for (int i = 1; i < argc; ++i) {
        const std::string arg {argv[i]};
        auto needsValue = [&](const std::string& flag) -> bool {
            if (arg != flag) return false;
            if (i + 1 >= argc) {
                std::cerr << "u273_bench: " << flag << " requires a value\n";
                o.ok = false;
                return true;
            }
            return true;
        };
        if (arg == "--help" || arg == "-h") {
            std::cout << "u273_bench [--output-dir DIR] [--dataset-dir DIR] [--sample-rate HZ]\n";
            o.ok = false;
            return o;
        } else if (needsValue("--output-dir")) {
            if (!o.ok) return o;
            o.outputDir = fs::path {argv[++i]};
        } else if (needsValue("--dataset-dir")) {
            if (!o.ok) return o;
            o.datasetDir = fs::path {argv[++i]};
        } else if (needsValue("--sample-rate")) {
            if (!o.ok) return o;
            try {
                o.sampleRate = std::stod(argv[++i]);
            } catch (...) {
                std::cerr << "u273_bench: invalid --sample-rate value\n";
                o.ok = false;
                return o;
            }
            if (!(o.sampleRate > 0.0) || !std::isfinite(o.sampleRate)) {
                std::cerr << "u273_bench: --sample-rate must be a positive finite number\n";
                o.ok = false;
                return o;
            }
        } else {
            std::cerr << "u273_bench: unknown argument '" << arg << "'\n";
            o.ok = false;
            return o;
        }
    }
    return o;
}

} // namespace

int main(int argc, char** argv)
{
    const auto cli = parseCli(argc, argv);
    if (!cli.ok) {
        return 1;
    }

    const auto stamp = timestampNow();
    std::vector<std::string> benchNotes {};
    std::vector<std::string> benchFailures {};

    std::error_code ec;
    fs::create_directories(cli.outputDir, ec);
    if (ec) {
        std::cerr << "u273_bench: could not create output directory '"
                  << cli.outputDir.string() << "': " << ec.message() << "\n";
        // Still try to emit a report into CWD as fallback? Per spec keep simple:
        // exit 0 because argv was well-formed; downstream PowerShell warns.
        return 0;
    }

    std::cout << "u273_bench: dataset-dir = " << cli.datasetDir.string() << "\n";
    std::cout << "u273_bench: output-dir  = " << cli.outputDir.string() << "\n";
    std::cout << "u273_bench: sample-rate = " << cli.sampleRate << " Hz\n";

    calib::OfflineCalibrationRunOptions runOptions {};
    runOptions.transient.sampleRate = cli.sampleRate;
    runOptions.transient.method = ss::IntegrationMethod::trBdf2;
    runOptions.enableBoundedCalibration = true;
    runOptions.enableIdentifiability = true;
    runOptions.enableAudioGate = true;

    const calib::B6B11CalibrationRunner runner {};
    const auto report = runner.runOffline(cli.datasetDir, runOptions);

    benchNotes.push_back("dataset directory: " + cli.datasetDir.string());
    benchNotes.push_back("sample rate: " + std::to_string(cli.sampleRate) + " Hz");

    // Load the same DC-execution netlist the runner used and apply the
    // calibrated parameters to obtain the circuit we drive with our test
    // signals. Failure here is recorded but never fatal.
    ss::CircuitGraph calibratedCircuit {};
    std::string commandSourceId {};
    double dcBias = 0.0;
    bool circuitReady = false;
    {
        const auto netlistPath = cli.datasetDir / "u273_netlist.json";
        ss::U273NetlistLoaderOptions loaderOptions {};
        loaderOptions.source = ss::U273NetlistSource::dcExecutionReference;
        const auto loaded = ss::U273NetlistLoader::loadFromFile(netlistPath, loaderOptions);
        if (!loaded.report.hasUsableCircuit()) {
            benchFailures.push_back("DC execution netlist loader did not produce a usable circuit");
        } else if (!report.parameters.isValid()) {
            benchFailures.push_back("calibrated parameters not valid; using defaults for signal drive");
            calibratedCircuit = applyParameters(loaded.circuit, calib::ActiveModelParameters {});
            commandSourceId = preferredCommandSourceId(calibratedCircuit);
            if (!commandSourceId.empty()) {
                dcBias = dcBiasFor(calibratedCircuit, commandSourceId);
                circuitReady = true;
            } else {
                benchFailures.push_back("no command voltage source in reference circuit");
            }
        } else {
            calibratedCircuit = applyParameters(loaded.circuit, report.parameters);
            commandSourceId = preferredCommandSourceId(calibratedCircuit);
            if (commandSourceId.empty()) {
                benchFailures.push_back("no command voltage source in calibrated circuit");
            } else {
                dcBias = dcBiasFor(calibratedCircuit, commandSourceId);
                circuitReady = true;
            }
        }
    }

    std::vector<u273::dsp::TableReductionPoint> realtimeReductionTable {};
    if (circuitReady && report.parameters.isValid()) {
        const calib::ReductionBuilder reductionBuilder {};
        calib::ReductionBuildOptions reductionOptions {};
        reductionOptions.sampleRate = cli.sampleRate;
        const auto reduction = reductionBuilder.build(report.parameters, calibratedCircuit, reductionOptions);
        if (reduction.built) {
            realtimeReductionTable = makeDspTable(reduction.points);
            const auto wroteReductionCsv =
                calib::writeReductionTableCsv(reduction, cli.outputDir / "reduction_table.csv");
            const auto wroteReductionJson =
                calib::writeReductionTableJson(reduction, cli.outputDir / "reduction_table.json");
            if (!wroteReductionCsv || !wroteReductionJson) {
                benchFailures.push_back("failed to export reduction table CSV/JSON assets");
            }
            benchNotes.push_back("reduction table built: points="
                                 + std::to_string(reduction.points.size())
                                 + ", monotonic=" + boolStr(reduction.monotonic)
                                 + ", smoothC1=" + boolStr(reduction.smoothC1));
        } else {
            benchFailures.push_back("ReductionBuilder did not produce a promotable table");
            for (const auto& failure : reduction.failures) {
                benchFailures.push_back("reduction table: " + failure);
            }
        }
    } else {
        benchNotes.push_back("reduction table skipped: calibrated circuit or parameters unavailable");
    }

    constexpr int kSamples = 8192;
    const auto sineInput  = synthesizeSine(kSamples, cli.sampleRate, 1000.0, -6.0);
    const auto kickInput  = synthesizeKick(kSamples, cli.sampleRate);
    const auto noiseInput = synthesizeNoise(kSamples);

    std::vector<SignalMetrics> signalResults {};
    signalResults.reserve(3);

    struct SignalSpec {
        const char* name;
        const char* inputCsv;
        const char* outputCsv;
        const std::vector<double>* input;
        bool measureThd;
    };
    const std::array<SignalSpec, 3> specs {{
        {"sine_1khz", "sine_1khz_input.csv", "sine_1khz_output.csv", &sineInput, true},
        {"kick",      "kick_input.csv",      "kick_output.csv",      &kickInput, false},
        {"noise",     "noise_input.csv",     "noise_output.csv",     &noiseInput, false}}};

    for (const auto& spec : specs) {
        SignalMetrics m {};
        m.name = spec.name;

        writeCsv(cli.outputDir / spec.inputCsv, *spec.input);

        std::vector<double> output {};
        if (circuitReady) {
            const auto drive = driveSignalThroughModel(calibratedCircuit,
                                                       commandSourceId,
                                                       dcBias,
                                                       *spec.input,
                                                       cli.sampleRate);
            output = drive.capturedOutput;
            m.drovenOk = drive.fullyConverged;
            for (const auto& f : drive.failures) {
                m.failures.push_back(f);
                benchFailures.push_back(std::string {spec.name} + ": " + f);
            }
        } else {
            output.assign(spec.input->size(), std::numeric_limits<double>::quiet_NaN());
        }

        writeCsv(cli.outputDir / spec.outputCsv, output);

        // Strip warmup samples for metrics so the kick window and the sine
        // settle. We keep the CSVs raw; only the metrics get the trimmed view.
        std::vector<double> inputTrim {};
        std::vector<double> outputTrim {};
        if (static_cast<int>(spec.input->size()) > kWarmupSamples) {
            inputTrim.assign(spec.input->begin() + kWarmupSamples, spec.input->end());
            outputTrim.assign(output.begin() + std::min<std::size_t>(kWarmupSamples, output.size()),
                              output.end());
        } else {
            inputTrim = *spec.input;
            outputTrim = output;
        }
        computeMetrics(m, inputTrim, outputTrim);

        if (spec.measureThd) {
            // Reuse the ThdBench result from the runner; it already drives a
            // 1 kHz tone through the same calibrated circuit.
            if (std::isfinite(report.thdBench.measuredThdDb)) {
                m.thdDb = report.thdBench.measuredThdDb;
            }
        }

        signalResults.push_back(m);
    }

    u273::core::ParameterSnapshot realtimeSnapshot {};
    realtimeSnapshot.inputGainDb = 0.0f;
    realtimeSnapshot.outputGainDb = 0.0f;
    realtimeSnapshot.drive = 1.0f;
    realtimeSnapshot.detectorScale = 1.0f;
    realtimeSnapshot.attackMs = 0.5f;
    realtimeSnapshot.releaseMs = 500.0f;
    realtimeSnapshot.mix = 1.0f;

    constexpr int kRealtimeBlockSize = 128;
    const auto oneSecondSamples = std::max(1, static_cast<int>(std::llround(cli.sampleRate)));
    const auto twoSecondSamples = std::max(1, static_cast<int>(std::llround(cli.sampleRate * 2.0)));
    const auto sweepEndHz = std::min(20000.0, cli.sampleRate * 0.45);

    struct RealtimeSpec {
        std::string name {};
        std::string description {};
        std::vector<float> input {};
        bool measureThd {};
        double thdFrequencyHz {};
    };
    std::vector<RealtimeSpec> realtimeSpecs {};
    realtimeSpecs.push_back(RealtimeSpec {
        "dsp_sine_100hz_1s",
        "100 Hz sine, 1 second, -6 dBFS peak",
        synthesizeSineFloat(oneSecondSamples, cli.sampleRate, 100.0, -6.0),
        true,
        100.0});
    realtimeSpecs.push_back(RealtimeSpec {
        "dsp_log_sweep_20hz_20khz_2s",
        "log sweep, 2 seconds, -6 dBFS peak",
        synthesizeLogSweepFloat(twoSecondSamples, cli.sampleRate, 20.0, sweepEndHz, -6.0),
        false,
        0.0});

    std::vector<RealtimeDspSignalMetrics> realtimeDspResults {};
    realtimeDspResults.reserve(realtimeSpecs.size());
    for (const auto& spec : realtimeSpecs) {
        RealtimeDspSignalMetrics metrics {};
        metrics.name = spec.name;
        metrics.description = spec.description;
        metrics.durationSeconds = static_cast<double>(spec.input.size()) / cli.sampleRate;
        metrics.sampleRate = cli.sampleRate;
        metrics.inputCsv = spec.name + "_input.csv";
        metrics.outputCsv = spec.name + "_output.csv";
        metrics.inputWav = spec.name + "_input.wav";
        metrics.outputWav = spec.name + "_output.wav";

        const auto rendered = renderRealtimeDspSignal(spec.input,
                                                      cli.sampleRate,
                                                      kRealtimeBlockSize,
                                                      realtimeSnapshot,
                                                      realtimeReductionTable.empty() ? nullptr : &realtimeReductionTable);
        metrics.processedOk = rendered.processedOk;
        metrics.clipped = rendered.clipped;
        metrics.usingReductionTable = rendered.usingReductionTable;
        metrics.boundary = rendered.boundary;
        metrics.maxMeterGrDb = rendered.maxMeterGrDb;
        metrics.avgMeterGrDb = rendered.avgMeterGrDb;
        metrics.failures = rendered.failures;
        fillRealtimeDspMetrics(metrics, spec.input, rendered.output);

        if (spec.measureThd) {
            const auto trimSamples = static_cast<int>(std::llround(cli.sampleRate * 0.25));
            const auto trimmedOutput = trimFront(rendered.output, trimSamples);
            metrics.thdDb = computeSineThdDb(trimmedOutput, cli.sampleRate, spec.thdFrequencyHz, 8);
        }

        writeCsv(cli.outputDir / metrics.inputCsv, spec.input);
        writeCsv(cli.outputDir / metrics.outputCsv, rendered.output);
        writeWavFloatMono(cli.outputDir / metrics.inputWav, spec.input, cli.sampleRate);
        writeWavFloatMono(cli.outputDir / metrics.outputWav, rendered.output, cli.sampleRate);

        for (const auto& failure : metrics.failures) {
            benchFailures.push_back("realtime DSP " + spec.name + ": " + failure);
        }
        realtimeDspResults.push_back(std::move(metrics));
    }
    writeRealtimeDspMetricsCsv(cli.outputDir / "realtime_dsp_audio_metrics.csv", realtimeDspResults);

    const auto aliasingMetrics = runAliasingBench(
        cli.sampleRate,
        realtimeSnapshot,
        realtimeReductionTable.empty() ? nullptr : &realtimeReductionTable,
        benchFailures);
    writeAliasingBenchCsv(cli.outputDir / "aliasing_smoothness_metrics.csv", aliasingMetrics);

    const auto reportPath = cli.outputDir / "SCIENTIFIC_REPORT.md";
    writeReport(reportPath,
                stamp,
                report,
                signalResults,
                realtimeDspResults,
                aliasingMetrics,
                benchNotes,
                benchFailures);

    std::cout << "u273_bench: report written to " << reportPath.string() << "\n";
    std::cout << "u273_bench: audio gate = " << boolStr(report.gates.audio)
              << ", canPromote = " << boolStr(report.canPromoteBoundary()) << "\n";
    return 0;
}
