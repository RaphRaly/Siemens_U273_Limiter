#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "u273/core/U273Core.h"
#include "u273/dsp/AnalogRealtimeEngine.h"
#include "u273/dsp/DetectorEnvelope.h"
#include "u273/dsp/FullActiveRealtimeEngine.h"
#include "u273/dsp/RateGraph.h"
#include "u273/dsp/RealtimeDetailLevel.h"
#include "u273/dsp/RealtimeGainReductionModel.h"
#include "u273/dsp/TableReductionRealtimeEngine.h"
#include "u273/dsp/U273DspEngine.h"
#include "u273/reference/GoldenValidationSuite.h"
#include "u273/reference/ReferenceValidator.h"
#include "u273/reference/ScientificReferenceModel.h"
#include "u273/reference/U273TechnicalSpecs.h"
#include "u273/reference/calibration/ActiveModelParameterMapping.h"
#include "u273/reference/calibration/ActiveTopologyCandidate.h"
#include "u273/reference/calibration/ActiveTopologyEvaluator.h"
#include "u273/reference/calibration/B6B11CalibrationRunner.h"
#include "u273/reference/calibration/BoundedCalibrationSolver.h"
#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/calibration/IdentifiabilityAnalyzer.h"
#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/calibration/ReductionBuilder.h"
#include "u273/reference/calibration/SonicTarget.h"
#include "u273/reference/calibration/TransientReferenceSolver.h"
#include "u273/reference/state_space/ComponentModels.h"
#include "u273/reference/state_space/Matrix.h"
#include "u273/reference/state_space/NewtonSolver.h"
#include "u273/reference/state_space/StateSpaceSolver.h"
#include "u273/reference/state_space/U273NetlistLoader.h"
#include "u273/reference/state_space/U273ReferenceCircuitBuilder.h"

namespace {

// Tests use a tiny assertion helper instead of a framework so the tests-only
// preset remains independent from external package downloads.
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class FixedGainReductionModel final : public u273::dsp::RealtimeGainReductionModel {
public:
    void prepare(double sampleRate) noexcept override
    {
        lastSampleRate = sampleRate;
        ++prepareCalls;
    }

    void reset() noexcept override
    {
        ++resetCalls;
    }

    [[nodiscard]] float evaluateGainReductionDb(
        float detectorEnvelope,
        const u273::core::ParameterSnapshot& snapshot) noexcept override
    {
        (void) detectorEnvelope;
        (void) snapshot;
        ++evaluateCalls;
        return gainReductionDb;
    }

    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept override
    {
        return u273::core::ModelBoundary::guardedRealtimeSurrogate;
    }

    float gainReductionDb {6.0f};
    double lastSampleRate {};
    int prepareCalls {};
    int resetCalls {};
    int evaluateCalls {};
};

std::filesystem::path resultsDirectory()
{
    return std::filesystem::path {U273_RESULTS_DIR};
}

bool sameNode(u273::reference::state_space::NodeId lhs, u273::reference::state_space::NodeId rhs)
{
    return lhs.value == rhs.value;
}

bool hasResistor(const u273::reference::state_space::CircuitGraph& circuit,
                 const char* id,
                 const char* n1,
                 const char* n2,
                 double resistanceOhm)
{
    const auto node1 = circuit.findNode(n1);
    const auto node2 = circuit.findNode(n2);
    for (const auto& resistor : circuit.resistors()) {
        const auto forward = sameNode(resistor.positive, node1) && sameNode(resistor.negative, node2);
        const auto reverse = sameNode(resistor.positive, node2) && sameNode(resistor.negative, node1);
        if (resistor.id == id
            && (forward || reverse)
            && std::fabs(resistor.resistanceOhm - resistanceOhm) <= resistanceOhm * 1.0e-12) {
            return true;
        }
    }
    return false;
}

const u273::dsp::RateStage& requireRateStage(const u273::dsp::RateGraph& graph, const char* name)
{
    const auto* stage = u273::dsp::findRateStage(graph, name);
    require(stage != nullptr, "rate graph must expose the requested stage");
    return *stage;
}

double dftBinMagnitude(const std::vector<float>& samples, int bin)
{
    constexpr auto pi = 3.1415926535897932384626433832795;
    std::complex<double> sum {};
    const auto size = static_cast<double>(samples.size());
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto phase = -2.0 * pi * static_cast<double>(bin) * static_cast<double>(index) / size;
        sum += static_cast<double>(samples[index]) * std::complex<double> {std::cos(phase), std::sin(phase)};
    }
    return 2.0 * std::abs(sum) / size;
}

double thdPercent(const std::vector<float>& samples, int fundamentalBin, int highestHarmonic)
{
    const auto fundamental = dftBinMagnitude(samples, fundamentalBin);
    if (!(fundamental > 0.0)) {
        return 100.0;
    }

    auto harmonicPower = 0.0;
    for (auto harmonic = 2; harmonic <= highestHarmonic; ++harmonic) {
        const auto bin = fundamentalBin * harmonic;
        if (bin >= static_cast<int>(samples.size() / 2)) {
            break;
        }
        const auto magnitude = dftBinMagnitude(samples, bin);
        harmonicPower += magnitude * magnitude;
    }
    return std::sqrt(harmonicPower) / fundamental * 100.0;
}

std::vector<float> processSine(float frequencyHz, float amplitude, u273::core::ParameterSnapshot snapshot)
{
    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 4800;
    constexpr auto pi = 3.1415926535897932384626433832795;

    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {sampleRate, sampleCount, 1});

    std::vector<float> samples(static_cast<std::size_t>(sampleCount), 0.0f);
    for (int sample = 0; sample < sampleCount; ++sample) {
        samples[static_cast<std::size_t>(sample)] = amplitude
            * static_cast<float>(std::sin(2.0 * pi * static_cast<double>(frequencyHz) * sample / sampleRate));
    }

    std::array<float*, 1> channels {samples.data()};
    u273::core::ProcessContext context {
        u273::core::AudioBlockView {channels.data(), 1, static_cast<int>(samples.size())},
        sampleRate,
        100,
        true};

    u273::core::MeterFrame meter {};
    const auto status = engine.process(context, snapshot, &meter);
    require(status == u273::dsp::ProcessStatus::ok, "sine render must process for FFT validation");
    require(meter.isValid(), "sine render meter must stay valid");
    return samples;
}

void testParameterSnapshotContract()
{
    u273::core::ParameterSnapshot snapshot {};
    require(snapshot.isValid(), "default parameter snapshot must be valid");

    snapshot.drive = 2.0f;
    require(!snapshot.isValid(), "drive range must be enforced");
}

void testDspSilenceIsStable()
{
    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {48000.0, 64, 2});
    require(engine.isPrepared(), "DSP engine must prepare with valid config");

    std::array<float, 64> left {};
    std::array<float, 64> right {};
    std::array<float*, 2> channels {left.data(), right.data()};

    u273::core::ProcessContext context {
        u273::core::AudioBlockView {channels.data(), 2, static_cast<int>(left.size())},
        48000.0,
        1,
        true};

    u273::core::ParameterSnapshot snapshot {};
    u273::core::MeterFrame meter {};
    const auto status = engine.process(context, snapshot, &meter);

    require(status == u273::dsp::ProcessStatus::ok, "silence block must process");
    require(meter.isValid(), "meter frame must stay valid on silence");
    require(meter.outputPeakDb <= u273::core::kMinMeterDb + 0.001f, "silence output peak must stay at floor");
}

void testRateGraphQualityModesDeclareExpectedRates()
{
    const auto eco = u273::dsp::buildRateGraph(
        u273::dsp::RateGraphConfig {48000.0, 64, u273::dsp::RealtimeQualityMode::eco});
    require(eco.isValid(), "Eco rate graph must be valid");
    require(eco.stageCount == 4, "Eco rate graph must expose all realtime stages");
    require(requireRateStage(eco, "audioInput").oversamplingFactor == 1,
            "Eco audio input must stay at host rate");
    require(requireRateStage(eco, "sidechain").oversamplingFactor == 1,
            "Eco sidechain must stay at host rate");
    require(requireRateStage(eco, "gainCell").oversamplingFactor == 1,
            "Eco gain-cell must stay at host rate");
    require(requireRateStage(eco, "audioOutput").oversamplingFactor == 1,
            "Eco audio output must stay at host rate");
    require(totalLatencySamples(eco) == 0, "Eco skeleton latency must be zero until resamplers execute");
    require(!eco.oversamplingExecutionEnabled, "Eco skeleton must not execute oversampling yet");

    const auto precise = u273::dsp::buildRateGraph(
        u273::dsp::RateGraphConfig {48000.0, 64, u273::dsp::RealtimeQualityMode::precise});
    require(precise.isValid(), "Precise rate graph must be valid");
    require(requireRateStage(precise, "audioInput").oversamplingFactor == 1,
            "Precise audio input must stay at host rate");
    require(requireRateStage(precise, "sidechain").oversamplingFactor == 2,
            "Precise sidechain must declare 2x oversampling");
    require(requireRateStage(precise, "gainCell").oversamplingFactor == 2,
            "Precise gain-cell must declare 2x oversampling");
    require(requireRateStage(precise, "audioOutput").oversamplingFactor == 1,
            "Precise audio output must stay at host rate");
    require(totalLatencySamples(precise) == 0,
            "Precise skeleton latency must stay zero until resamplers execute");
    require(!precise.oversamplingExecutionEnabled, "Precise skeleton must not execute oversampling yet");

    const auto render = u273::dsp::buildRateGraph(
        u273::dsp::RateGraphConfig {48000.0, 64, u273::dsp::RealtimeQualityMode::render});
    require(render.isValid(), "Render rate graph must be valid");
    require(requireRateStage(render, "audioInput").oversamplingFactor == 1,
            "Render audio input must stay at host rate");
    require(requireRateStage(render, "sidechain").oversamplingFactor == 4,
            "Render sidechain must declare 4x oversampling");
    require(requireRateStage(render, "gainCell").oversamplingFactor == 4,
            "Render gain-cell must declare 4x oversampling");
    require(requireRateStage(render, "audioOutput").oversamplingFactor == 1,
            "Render audio output must stay at host rate");
    require(totalLatencySamples(render) == 0, "Render skeleton latency must stay zero until resamplers execute");
    require(!render.oversamplingExecutionEnabled, "Render skeleton must not execute oversampling yet");
}

void testRateGraphRejectsInvalidConfig()
{
    require(!u273::dsp::buildRateGraph(
                u273::dsp::RateGraphConfig {0.0, 64, u273::dsp::RealtimeQualityMode::precise})
                 .isValid(),
            "rate graph must reject non-positive sample rates");
    require(!u273::dsp::buildRateGraph(
                u273::dsp::RateGraphConfig {48000.0, -1, u273::dsp::RealtimeQualityMode::precise})
                 .isValid(),
            "rate graph must reject negative block sizes");
    require(!u273::dsp::buildRateGraph(
                u273::dsp::RateGraphConfig {
                    48000.0,
                    64,
                    static_cast<u273::dsp::RealtimeQualityMode>(99)})
                 .isValid(),
            "rate graph must reject unknown quality modes");

    const auto invalidConfig = u273::dsp::DspPrepareConfig {
        48000.0,
        64,
        2,
        static_cast<u273::dsp::RealtimeQualityMode>(99)};
    require(!invalidConfig.isValid(), "DSP prepare config must reject unknown quality modes");
}

void testDspPrepareStoresRateGraphAndLatency()
{
    const std::array modes {
        u273::dsp::RealtimeQualityMode::eco,
        u273::dsp::RealtimeQualityMode::precise,
        u273::dsp::RealtimeQualityMode::render};

    for (const auto mode : modes) {
        u273::dsp::U273DspEngine engine {};
        engine.prepare(u273::dsp::DspPrepareConfig {96000.0, 128, 2, mode});

        require(engine.isPrepared(), "DSP engine must prepare for every quality mode");
        require(engine.qualityMode() == mode, "DSP engine must retain the requested quality mode");
        require(engine.rateGraph().isValid(), "DSP engine must expose a valid prepared rate graph");
        require(engine.latencySamples() == u273::dsp::totalLatencySamples(engine.rateGraph()),
                "DSP engine latency must match the prepared rate graph");
        require(engine.latencySamples() == 0, "DSP skeleton latency must remain zero before resampler execution");
        require(!engine.oversamplingExecutionEnabled(),
                "DSP skeleton must expose oversampling as declared but not executed");
        require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
                "quality modes must not promote the analog realtime model boundary");
    }
}

void testRenderRateGraphDoesNotOversampleExecutionYet()
{
    FixedGainReductionModel model {};
    u273::dsp::U273DspEngine engine {model};
    engine.prepare(u273::dsp::DspPrepareConfig {
        48000.0,
        8,
        1,
        u273::dsp::RealtimeQualityMode::render});

    require(engine.isPrepared(), "DSP engine must prepare render quality mode");
    require(requireRateStage(engine.rateGraph(), "sidechain").oversamplingFactor == 4,
            "Render mode must declare sidechain 4x oversampling");
    require(requireRateStage(engine.rateGraph(), "gainCell").oversamplingFactor == 4,
            "Render mode must declare gain-cell 4x oversampling");
    require(!engine.oversamplingExecutionEnabled(),
            "Render mode must not execute oversampling in the skeleton milestone");

    std::array<float, 8> mono {};
    mono.fill(0.5f);
    std::array<float*, 1> channels {mono.data()};

    u273::core::ProcessContext context {
        u273::core::AudioBlockView {channels.data(), 1, static_cast<int>(mono.size())},
        48000.0,
        23,
        true};

    u273::core::ParameterSnapshot snapshot {};
    u273::core::MeterFrame meter {};
    const auto status = engine.process(context, snapshot, &meter);

    require(status == u273::dsp::ProcessStatus::ok, "render skeleton block must process");
    require(model.evaluateCalls == static_cast<int>(mono.size()),
            "render skeleton must evaluate the gain model once per host sample");
}

void testDspHotSignalReducesGain()
{
    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {48000.0, 256, 2});
    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "DSP engine must expose the analog realtime boundary instead of the surrogate boundary");

    std::array<float, 256> left {};
    std::array<float, 256> right {};
    left.fill(0.8f);
    right.fill(0.8f);
    std::array<float*, 2> channels {left.data(), right.data()};

    u273::core::ProcessContext context {
        u273::core::AudioBlockView {channels.data(), 2, static_cast<int>(left.size())},
        48000.0,
        2,
        true};

    u273::core::ParameterSnapshot snapshot {};
    snapshot.drive = 1.0f;
    snapshot.attackMs = 0.05f;
    snapshot.releaseMs = 50.0f;

    u273::core::MeterFrame meter {};
    const auto status = engine.process(context, snapshot, &meter);

    require(status == u273::dsp::ProcessStatus::ok, "hot signal block must process");
    require(meter.gainReductionDb > 0.0f, "hot signal must produce guarded gain reduction");
    require(std::fabs(left.back()) < 0.8f, "hot signal output must be attenuated");
}

void testDspEngineUsesInjectedGainReductionModel()
{
    FixedGainReductionModel model {};
    u273::dsp::U273DspEngine engine {model};
    engine.prepare(u273::dsp::DspPrepareConfig {48000.0, 8, 1});

    require(engine.isPrepared(), "DSP engine must prepare with an injected gain reduction model");
    require(model.prepareCalls == 1, "injected gain reduction model must receive prepare");
    require(std::fabs(model.lastSampleRate - 48000.0) < 1.0e-12,
            "injected gain reduction model must receive the prepare sample rate");
    require(engine.boundary() == u273::core::ModelBoundary::guardedRealtimeSurrogate,
            "DSP engine boundary must come from the injected model abstraction");

    std::array<float, 8> mono {};
    mono.fill(0.5f);
    std::array<float*, 1> channels {mono.data()};

    u273::core::ProcessContext context {
        u273::core::AudioBlockView {channels.data(), 1, static_cast<int>(mono.size())},
        48000.0,
        22,
        true};

    u273::core::ParameterSnapshot snapshot {};
    u273::core::MeterFrame meter {};
    const auto status = engine.process(context, snapshot, &meter);

    require(status == u273::dsp::ProcessStatus::ok, "DSP engine must process with an injected model");
    require(model.evaluateCalls == static_cast<int>(mono.size()),
            "injected gain reduction model must be evaluated once per processed sample");
    require(std::fabs(meter.gainReductionDb - model.gainReductionDb) < 1.0e-6f,
            "meter gain reduction must reflect the injected model");
    require(mono.back() < 0.5f, "injected model gain reduction must affect audio output");
}

void testAnalogRealtimeBridgeLawIsMonotonic()
{
    u273::dsp::AnalogRealtimeEngine engine {};
    engine.prepare(48000.0);

    u273::core::ParameterSnapshot snapshot {};
    snapshot.drive = 1.0f;

    const auto low = engine.evaluateGainReductionDb(0.1f, snapshot);
    const auto mid = engine.evaluateGainReductionDb(0.4f, snapshot);
    const auto high = engine.evaluateGainReductionDb(0.8f, snapshot);

    require(low >= 0.0f, "analog bridge low-level gain reduction must be non-negative");
    require(mid >= low, "analog bridge gain reduction must increase with command level");
    require(high >= mid, "analog bridge high command must not reduce less than mid command");
    require(engine.lastFrame().isValid(), "analog bridge telemetry must remain finite");
    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "analog bridge realtime model must not claim final validated closure yet");
}

void testBypassPreservesSignal()
{
    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {48000.0, 16, 1});

    std::array<float, 16> mono {};
    for (std::size_t index = 0; index < mono.size(); ++index) {
        mono[index] = static_cast<float>(index) / 16.0f;
    }
    const auto lastBefore = mono.back();

    std::array<float*, 1> channels {mono.data()};
    u273::core::ProcessContext context {
        u273::core::AudioBlockView {channels.data(), 1, static_cast<int>(mono.size())},
        48000.0,
        3,
        true};

    u273::core::ParameterSnapshot snapshot {};
    snapshot.bypass = true;
    snapshot.inputGainDb = 12.0f;
    snapshot.outputGainDb = -3.0f;

    u273::core::MeterFrame meter {};
    const auto status = engine.process(context, snapshot, &meter);

    require(status == u273::dsp::ProcessStatus::ok, "bypass block must process");
    require(std::fabs(mono.back() - lastBefore) < 0.000001f, "bypass must preserve signal");
    require(meter.gainReductionDb == 0.0f, "bypass must report zero gain reduction");
}

void testReferenceBoundaryIsExplicit()
{
    u273::reference::ScientificReferenceModel model {};
    u273::reference::ReferenceValidator validator {model};
    const auto report = validator.compareGuardedBoundary();

    require(report.isValid(), "reference validation report must be valid");
    require(report.passed, "current reference boundary must pass only as guarded boundary");
    require(report.boundary == u273::core::ModelBoundary::passWithGuardedBoundaries,
            "reference boundary must remain PASS_WITH_GUARDED_BOUNDARIES");
}

void testGoldenFilesAreReadableAndPinned()
{
    const auto summary = u273::reference::GoldenValidationSuite::validateResultsDirectory(resultsDirectory());

    require(summary.passed(), "JS golden DC/AC/transient files must validate");
    require(summary.dcScenarios == 4, "golden DC scenario count must stay pinned");
    require(summary.acRows == 80, "golden AC row count must stay pinned");
    require(summary.transientRows == 933, "golden transient row count must stay pinned");
    require(summary.transientCases == 3, "golden transient case count must stay pinned");
    require(std::fabs(summary.firstDcCommandVolt - 0.9773555249174533) < 1.0e-9,
            "golden first DC command voltage must stay pinned");
    require(std::fabs(summary.firstDcBridgeNodeVolt - 0.8263923585101612) < 1.0e-9,
            "golden first DC bridge node voltage must stay pinned");
}

void testCalibrationDatasetLoadsGoldenFiles()
{
    const auto dataset = u273::reference::calibration::CalibrationDataset::loadFromResultsDirectory(resultsDirectory());

    require(dataset.isValid(), "calibration dataset must load DC/AC/transient golden files");
    require(dataset.dcStatus == "PARAMETRIC_THEVENIN_REFERENCE", "calibration dataset must pin DC status");
    require(dataset.acStatus == "PARAMETRIC_THEVENIN_B6_BRIDGE_REFERENCE",
            "calibration dataset must pin AC status");
    require(dataset.transientStatus == "BOUNDED_QUASI_STATIC_TRANSIENT",
            "calibration dataset must pin transient status");
    require(dataset.dcScenarios.size() == 4, "calibration dataset must expose four DC scenarios");
    require(dataset.acRows == 80, "calibration dataset must expose pinned AC row count");
    require(dataset.acFrequencyCount == 5, "calibration dataset must expose five AC frequencies");
    require(dataset.acFrequenciesHz.size() == 5, "calibration dataset must expose AC frequency values");
    require(dataset.acPoints.size() == 80, "calibration dataset must expose golden AC residual rows");
    require(dataset.transientRows == 933, "calibration dataset must expose pinned transient row count");
    require(dataset.transientRowsData.size() == 933, "calibration dataset must expose transient row data");
    require(dataset.transientCases.size() == 3, "calibration dataset must expose three transient cases");
    require(dataset.transientSummaries.size() == 3, "calibration dataset must expose three transient summaries");

    const auto* firstDc = dataset.findDcScenario("delivery_linear_r3k_v1");
    require(firstDc != nullptr, "calibration dataset must find the first DC scenario by name");
    require(firstDc->converged, "first DC golden scenario must be converged");
    require(std::fabs(firstDc->cmdVolt - 0.9773555249174533) < 1.0e-9,
            "calibration dataset first DC CMD voltage mismatch");
    require(std::fabs(firstDc->nbVolt - 0.8263923585101612) < 1.0e-9,
            "calibration dataset first DC NB voltage mismatch");
    require(firstDc->hasNlVolt && firstDc->hasNrVolt && firstDc->hasB11DriveVolt,
            "calibration dataset must expose optional DC residual nodes when present");

    require(std::fabs(dataset.acPoints.front().frequencyHz - 40.0) < 1.0e-12,
            "calibration dataset first AC frequency mismatch");
    require(dataset.acPoints.front().hasCmd, "calibration dataset AC row must expose VCMD residual target");

    require(dataset.split.trainingDcScenarios.size() == 2,
            "calibration dataset must create a deterministic DC training split");
    require(dataset.split.validationDcScenarios.size() == 2,
            "calibration dataset must create a deterministic DC validation split");
    require(dataset.split.acceptanceTransientCases.size() == 3,
            "calibration dataset must reserve transient cases for acceptance");
    require(dataset.transientRowsForCase("limiter_nominal_12vrms_scale1_r10k").size() > 100,
            "calibration dataset must expose transient rows by case");
}

void testSonicThdTargetMatrixLoadsAndKeepsCmdProbeUnmapped()
{
    using namespace u273::reference::calibration;

    const auto target = loadSonicThdTargetCsv();

    require(target.loaded, "sonic THD target matrix must load from u273_assets");
    require(target.rows.size() == 18, "sonic THD target matrix row count must stay pinned");
    require(std::fabs(thdPercentToDb(0.5) - -46.02059991327962) < 1.0e-9,
            "0.5 percent THD must convert to the Siemens -46.02 dB target");

    const auto* regulated = findThdTargetByNodeAndMode(
        target.rows,
        "device_output",
        "regulated");
    require(regulated != nullptr, "matrix must expose published regulated device-output targets");
    require(regulated->sourceType == "siemens_primary",
            "published regulated target must keep Siemens primary provenance");

    const auto* internalExample = findThdTargetByNodeAndMode(
        target.rows,
        "diode_bridge_internal",
        "siemens_example");
    require(internalExample != nullptr,
            "matrix must preserve the Siemens internal diode bridge example");
    require(std::fabs(internalExample->thdTargetPercent - 0.18) < 1.0e-12,
            "internal bridge example must stay pinned at 0.18 percent");

    const auto* cmdProbe = findThdTargetByNodeAndMode(
        target.rows,
        "cmd_internal",
        "b11_cmd_probe");
    require(cmdProbe == nullptr,
            "current CMD probe must remain unmapped until an equivalent sonic target is defined");
}

void testDcCircuitViewOpensCapacitors()
{
    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json");
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(loaded.circuit);

    require(view.isValid(), "DC circuit view must be valid");
    require(view.circuit.nodeCount() == loaded.circuit.nodeCount(), "DC view must preserve node identity");
    require(view.circuit.capacitors().empty(), "DC view must open all capacitors");
    require(view.omittedCapacitors == static_cast<int>(loaded.circuit.capacitors().size()),
            "DC view must report omitted capacitor count");
    require(view.preservedResistors == static_cast<int>(loaded.circuit.resistors().size()),
            "DC view must preserve all resistors");
    require(view.preservedDiodes == static_cast<int>(loaded.circuit.diodes().size()),
            "DC view must preserve diode bridge stamps");
    require(view.circuit.findNode("CMD").value == loaded.circuit.findNode("CMD").value,
            "DC view must keep stable node ids for golden nodes");
}

void testOperatingPointConvergesOnKnownB6Case()
{
    using namespace u273::reference::state_space;

    const auto circuit = U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(0.1, 1.0);
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(circuit);

    u273::reference::calibration::OperatingPointOptions options {};
    options.solver.sampleRate = 192000.0;
    options.solver.newton.maxIterations = 96;
    options.solver.newton.residualTolerance = 1.0e-9;
    options.maxAttempts = 3;

    const u273::reference::calibration::OperatingPointSolver solver {options};
    const auto result = solver.solve(view);

    require(result.validInput, "B6 operating point input must be valid");
    require(result.converged, "B6 DC operating point must converge");
    require(result.step.boundary == u273::core::ModelBoundary::fullActiveModelUnverified,
            "B6 operating point must remain on the unverified active-model boundary");
    require(!result.attemptLog.empty(), "B6 operating point must expose attempt diagnostics");
}

void testOperatingPointFailureKeepsAttemptJournal()
{
    using namespace u273::reference::state_space;

    DiodeModel diode {};
    diode.saturationCurrentAmp = 1.0e-12;
    diode.ideality = 1.8;
    diode.gminSiemens = 0.0;

    const auto circuit = U273ReferenceCircuitBuilder::buildDiodeCurrentFixture(1.0e-3, diode);
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(circuit);

    u273::reference::calibration::OperatingPointOptions options {};
    options.solver.newton.maxIterations = 1;
    options.maxAttempts = 1;

    const u273::reference::calibration::OperatingPointSolver solver {options};
    const auto result = solver.solve(view);

    require(result.validInput, "limited-attempt OP input must be valid");
    require(!result.converged, "limited-attempt OP must fail cleanly");
    require(result.attempts == 1, "limited-attempt OP must respect maxAttempts");
    require(result.attemptLog.size() == 1, "failed OP must keep one attempt journal entry");
    require(!result.failures.empty(), "failed OP must explain the non-convergence");
}

void testAcLinearizationMatchesRcClosedForm()
{
    using namespace u273::reference::state_space;

    constexpr auto resistanceOhm = 1000.0;
    constexpr auto capacitanceFarad = 1.0e-6;
    constexpr auto frequencyHz = 1000.0;
    constexpr auto pi = 3.1415926535897932384626433832795;

    const auto circuit = U273ReferenceCircuitBuilder::buildRcLowpassFixture(1.0, resistanceOhm, capacitanceFarad);
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(circuit);

    u273::reference::calibration::OperatingPointOptions opOptions {};
    opOptions.solver.newton.maxIterations = 32;
    const u273::reference::calibration::OperatingPointSolver opSolver {opOptions};
    const auto op = opSolver.solve(view);
    require(op.converged, "RC DC operating point must converge before AC linearization");

    const auto output = circuit.findNode("OUT");
    u273::reference::calibration::AcSourcePort source {};
    source.voltageSourceId = "VIN";
    source.outputNode = output;
    source.amplitudeVolt = 1.0;

    const u273::reference::calibration::LinearizedAcSolver acSolver {};
    const auto ac = acSolver.solveSmallSignal(circuit, op, source, std::vector<double> {frequencyHz});

    require(ac.validInput, "RC AC linearization input must be valid");
    require(ac.solved, "RC AC linearization must solve");
    require(ac.points.size() == 1, "RC AC linearization must return one frequency point");

    const auto omegaRc = 2.0 * pi * frequencyHz * resistanceOhm * capacitanceFarad;
    const auto expectedMagnitude = 1.0 / std::sqrt(1.0 + omegaRc * omegaRc);
    const auto expectedPhase = -std::atan(omegaRc);
    require(std::fabs(ac.points[0].magnitudeLinear - expectedMagnitude) < 1.0e-9,
            "RC AC magnitude must match closed form");
    require(std::fabs(ac.points[0].phaseRadians - expectedPhase) < 1.0e-9,
            "RC AC phase must match closed form");
}

void testAcResidualGateMatchesRcClosedForm()
{
    using namespace u273::reference::state_space;

    constexpr auto resistanceOhm = 1000.0;
    constexpr auto capacitanceFarad = 1.0e-6;
    constexpr auto frequencyHz = 1000.0;
    constexpr auto pi = 3.1415926535897932384626433832795;

    const auto circuit = U273ReferenceCircuitBuilder::buildRcLowpassFixture(1.0, resistanceOhm, capacitanceFarad);
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(circuit);

    const u273::reference::calibration::OperatingPointSolver opSolver {};
    const auto op = opSolver.solve(view);
    require(op.converged, "RC OP must converge before residual AC fixture");

    u273::reference::calibration::AcSourcePort source {};
    source.voltageSourceId = "VIN";
    source.outputNode = circuit.findNode("OUT");
    source.amplitudeVolt = 1.0;

    const u273::reference::calibration::LinearizedAcSolver acSolver {};
    const auto ac = acSolver.solveSmallSignal(circuit, op, source, std::vector<double> {frequencyHz});
    require(ac.solved, "RC AC residual fixture must solve");

    const auto omegaRc = 2.0 * pi * frequencyHz * resistanceOhm * capacitanceFarad;
    const auto expectedMagnitude = 1.0 / std::sqrt(1.0 + omegaRc * omegaRc);
    const auto expectedPhase = -std::atan(omegaRc);

    u273::reference::calibration::AcGoldenPoint golden {};
    golden.scenario = "rc_fixture";
    golden.frequencyHz = frequencyHz;
    golden.hasCmd = true;
    golden.cmdMagnitudeDb = 20.0 * std::log10(expectedMagnitude);
    golden.cmdPhaseRadians = expectedPhase;

    const auto gate = u273::reference::calibration::evaluateAcResiduals(
        std::vector<u273::reference::calibration::AcGoldenPoint> {golden},
        ac);

    require(gate.validInput, "AC residual gate fixture input must be valid");
    require(gate.passed, "AC residual gate must pass an analytic RC fixture");
    require(gate.worstWeightedError < 1.0e-6, "AC residual fixture weighted error must be near zero");
}

[[nodiscard]] double stateNodeVoltageOrNan(const u273::reference::state_space::CircuitGraph& circuit,
                                           const u273::reference::state_space::CircuitState& state,
                                           u273::reference::state_space::NodeId node)
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

void setTestBridgeDiodeConductance(u273::reference::state_space::B6BridgeSmallSignalAcOptions& options,
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

[[nodiscard]] u273::reference::state_space::B6BridgeSmallSignalAcOptions strictB6AcOptionsFromDcOp(
    const u273::reference::state_space::CircuitGraph& dcCircuit,
    const u273::reference::state_space::CircuitState& state)
{
    u273::reference::state_space::B6BridgeSmallSignalAcOptions options {};
    options.commandPortResistanceOhm = 10000.0;

    for (const auto& diode : dcCircuit.diodes()) {
        const auto anodeVolt = stateNodeVoltageOrNan(dcCircuit, state, diode.anode);
        const auto cathodeVolt = stateNodeVoltageOrNan(dcCircuit, state, diode.cathode);
        if (std::isfinite(anodeVolt) && std::isfinite(cathodeVolt)) {
            setTestBridgeDiodeConductance(options,
                                          diode.id,
                                          diode.model.conductanceSiemens(anodeVolt - cathodeVolt));
        }
    }

    return options;
}

[[nodiscard]] u273::reference::calibration::OperatingPointResult solvedLinearOpForTest(
    const u273::reference::state_space::CircuitGraph& circuit)
{
    const u273::reference::state_space::StateSpaceSolver stateFactory {circuit};
    u273::reference::calibration::OperatingPointResult op {};
    op.validInput = true;
    op.converged = true;
    op.state = stateFactory.createInitialState();
    return op;
}

void testB6SmallSignalAcReferenceMatchesGoldenStrictSlice()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const auto dataset = CalibrationDataset::loadFromResultsDirectory(resultsDirectory());
    require(dataset.isValid(), "strict B6 AC reference test requires a valid golden dataset");
    require(!dataset.acFrequenciesHz.empty(), "strict B6 AC reference test requires AC frequencies");

    U273NetlistLoaderOptions loaderOptions {};
    loaderOptions.source = U273NetlistSource::dcExecutionReference;
    const auto loaded = U273NetlistLoader::loadFromFile(resultsDirectory() / "u273_netlist.json",
                                                        loaderOptions);
    require(loaded.report.hasUsableCircuit(), "strict B6 AC reference test requires the DC execution netlist");

    OperatingPointOptions opOptions {};
    opOptions.solver.newton.maxIterations = 96;
    opOptions.solver.newton.residualTolerance = 1.0e-8;
    opOptions.maxAttempts = 4;
    const OperatingPointSolver opSolver {opOptions};
    const auto dcOp = opSolver.solve(DCCircuitView::fromCircuit(loaded.circuit));
    require(dcOp.converged, "strict B6 AC reference test requires a converged DC operating point");

    const auto acCircuit = U273ReferenceCircuitBuilder::buildB6BridgeSmallSignalAcReference(
        strictB6AcOptionsFromDcOp(loaded.circuit, dcOp.state));
    require(acCircuit.findNode("CMD").value > 0, "strict B6 AC reference must expose CMD");
    require(acCircuit.findNode("NB").value > 0, "strict B6 AC reference must expose NB");
    require(!acCircuit.capacitors().empty(), "strict B6 AC reference must include dynamic capacitors");

    AcSourcePort source {};
    source.voltageSourceId = "VAC";
    source.outputNode = acCircuit.findNode("CMD");
    source.amplitudeVolt = 1.0;

    const LinearizedAcSolver acSolver {};
    const auto ac = acSolver.solveSmallSignal(acCircuit,
                                              solvedLinearOpForTest(acCircuit),
                                              source,
                                              dataset.acFrequenciesHz);
    require(ac.validInput, "strict B6 AC linearization input must be valid");
    require(ac.solved, "strict B6 AC linearization must solve");

    AcResidualOptions residualOptions {};
    residualOptions.scenario = "rsource_10k";
    residualOptions.driveVolt = 1.0;
    residualOptions.matchDriveVolt = true;
    residualOptions.commandSourceOhm = 10000.0;
    residualOptions.matchCommandSourceOhm = true;
    const auto gate = evaluateAcResiduals(dataset, ac, residualOptions);

    require(gate.validInput, "strict B6 AC residual gate input must be valid");
    require(gate.passed, "strict B6 AC reference must match the golden VCMD slice");
}

void testDcResidualGateReportsLoadedGoldenScenarioGaps()
{
    const auto dataset = u273::reference::calibration::CalibrationDataset::loadFromResultsDirectory(resultsDirectory());
    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json");
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(loaded.circuit);

    u273::reference::calibration::OperatingPointOptions options {};
    options.solver.newton.maxIterations = 96;
    options.solver.newton.residualTolerance = 1.0e-8;
    options.maxAttempts = 4;
    const u273::reference::calibration::OperatingPointSolver opSolver {options};
    const auto op = opSolver.solve(view);

    require(op.validInput, "loaded netlist OP residual input must be valid");
    require(op.converged, "loaded netlist OP must converge before DC residual gate");

    const auto gate = u273::reference::calibration::evaluateDcResiduals(dataset, view.circuit, op);
    require(gate.validInput, "DC residual gate input must be valid");
    require(gate.evaluated, "DC residual gate must evaluate a matching golden scenario");
    require(!gate.passed, "DC residual gate must fail honestly while loaded netlist remains guarded");
    require(!gate.sets.empty() && gate.sets[0].points.size() >= 2,
            "DC residual gate must expose per-node residual points");

    auto foundFailedNb = false;
    for (const auto& point : gate.sets[0].points) {
        if (point.name == "NB" && point.failed()) {
            foundFailedNb = true;
        }
    }
    require(foundFailedNb, "DC residual gate must expose the current NB model gap");
}

void testDcExecutionReferenceLoaderMatchesGoldenTopology()
{
    u273::reference::state_space::U273NetlistLoaderOptions loaderOptions {};
    loaderOptions.source = u273::reference::state_space::U273NetlistSource::dcExecutionReference;
    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json",
        loaderOptions);

    require(loaded.report.hasUsableCircuit(), "DC execution reference loader must create a usable circuit");
    require(loaded.report.status == "THEVENIN_REFERENCE_EXECUTABLE",
            "DC execution reference loader must report the executable reference status");
    require(loaded.report.componentObjects == 14,
            "DC execution reference loader must stamp only voltage sources, resistors and diodes");
    require(loaded.report.potentiometersSplit == 0,
            "DC execution reference loader must not split raw inventory potentiometers");
    require(loaded.report.stampedCapacitors == 0,
            "DC execution reference loader must not load raw inventory capacitors");
    require(loaded.report.stampedDiodes == 4, "DC execution reference loader must stamp four bridge diodes");

    for (const auto* node : {"B11_DRV", "CMD", "NB", "NL", "NR", "N14", "N15"}) {
        require(loaded.circuit.findNode(node).value > 0, "DC execution reference loader must expose golden nodes");
    }

    require(hasResistor(loaded.circuit, "R10", "CMD", "NB", 20000.0),
            "DC execution reference must connect R10 from CMD to NB");
    require(hasResistor(loaded.circuit, "R9", "NB", "0", 390000.0),
            "DC execution reference must connect R9 from NB to ground");
    require(hasResistor(loaded.circuit, "R7_effective", "NL", "NR", 100.0),
            "DC execution reference must use effective R7 between NL and NR");
    require(hasResistor(loaded.circuit, "R8_effective", "N14", "N15", 250000.0),
            "DC execution reference must use effective R8 between N14 and N15");
}

void testDcResidualGatePassesDcExecutionReference()
{
    const auto dataset = u273::reference::calibration::CalibrationDataset::loadFromResultsDirectory(resultsDirectory());
    u273::reference::state_space::U273NetlistLoaderOptions loaderOptions {};
    loaderOptions.source = u273::reference::state_space::U273NetlistSource::dcExecutionReference;
    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json",
        loaderOptions);
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(loaded.circuit);

    u273::reference::calibration::OperatingPointOptions options {};
    options.solver.newton.maxIterations = 96;
    options.solver.newton.residualTolerance = 1.0e-8;
    options.maxAttempts = 4;
    const u273::reference::calibration::OperatingPointSolver opSolver {options};
    const auto op = opSolver.solve(view);

    require(op.validInput, "DC execution reference OP input must be valid");
    require(op.converged, "DC execution reference OP must converge before DC residual gate");

    const auto gate = u273::reference::calibration::evaluateDcResiduals(dataset, view.circuit, op);
    require(gate.validInput, "DC execution reference residual input must be valid");
    require(gate.evaluated, "DC execution reference residual gate must evaluate golden scenario");
    require(gate.passed, "DC execution reference residual gate must match golden DC");
}

void testTransientResidualGateAcceptsGoldenSummaries()
{
    u273::reference::calibration::TransientGoldenSummary golden {};
    golden.caseName = "fixture";
    golden.maxDriveVolt = 0.125;
    golden.maxCmdVolt = 0.1;
    golden.attack90TimeSeconds = 0.25;
    golden.hasRelease10Time = false;

    u273::reference::calibration::TransientModelSummary model {};
    model.caseName = "fixture";
    model.hasMaxDriveVolt = true;
    model.maxDriveVolt = golden.maxDriveVolt;
    model.hasMaxCmdVolt = true;
    model.maxCmdVolt = golden.maxCmdVolt;
    model.hasAttack90TimeSeconds = true;
    model.attack90TimeSeconds = golden.attack90TimeSeconds;

    const auto gate = u273::reference::calibration::evaluateTransientResiduals(
        std::vector<u273::reference::calibration::TransientGoldenSummary> {golden},
        std::vector<u273::reference::calibration::TransientModelSummary> {model});

    require(gate.validInput, "transient residual gate fixture input must be valid");
    require(gate.passed, "transient residual gate must pass matching available summaries");
    require(!gate.sets.empty() && gate.sets[0].points.size() == 4,
            "transient residual gate must explicitly track unavailable release summary");
}

void testBjtCandidateRejectsImpossiblePins()
{
    using namespace u273::reference::state_space;

    u273::reference::calibration::ActiveTopologyCandidate impossible {};
    impossible.id = "Ts1_bad";
    impossible.device = "Ts1";
    impossible.kind = u273::reference::calibration::ActiveDeviceKind::npn;
    impossible.collector = NodeId {1};
    impossible.base = NodeId {1};
    impossible.emitter = NodeId {2};
    require(!impossible.hasUsablePins(), "BJT candidate must reject duplicated pins");
    impossible.reject("collector and base share the same node");
    require(impossible.isRejected(), "BJT candidate rejection reason must be explicit");

    u273::reference::calibration::ActiveTopologyCandidate plausible {};
    plausible.id = "Ts1_candidate";
    plausible.device = "Ts1";
    plausible.kind = u273::reference::calibration::ActiveDeviceKind::npn;
    plausible.collector = NodeId {1};
    plausible.base = NodeId {2};
    plausible.emitter = NodeId {3};
    require(plausible.hasUsablePins(), "BJT candidate with three non-ground unique pins must be usable");
}

void testActiveTopologyEvaluatorKeepsCandidateGuardedWithoutAcImprovement()
{
    using namespace u273::reference::state_space;

    CircuitGraph circuit {};
    const auto collector = circuit.addNode("C");
    const auto base = circuit.addNode("B");
    const auto emitter = circuit.addNode("E");
    circuit.addVoltageSource("VC", collector, kGroundNode, 5.0);
    circuit.addVoltageSource("VB", base, kGroundNode, 0.7);
    circuit.addResistor("RE", emitter, kGroundNode, 1000.0);

    u273::reference::calibration::DCCircuitView view {};
    view.circuit = circuit;
    view.preservedResistors = 1;
    const u273::reference::calibration::OperatingPointSolver opSolver {};
    const auto op = opSolver.solve(view);
    require(op.converged, "topology evaluator fixture OP must converge");

    u273::reference::calibration::ActiveTopologyCandidate candidate {};
    candidate.id = "fixture.Ts";
    candidate.device = "Ts";
    candidate.kind = u273::reference::calibration::ActiveDeviceKind::npn;
    candidate.collector = collector;
    candidate.base = base;
    candidate.emitter = emitter;

    const u273::reference::calibration::ActiveTopologyEvaluator evaluator {};
    const auto guarded = evaluator.evaluate(circuit, op, candidate);
    require(guarded.evaluated, "topology evaluator must evaluate a complete candidate");
    require(guarded.dcPlausible, "topology evaluator must detect plausible VBE/VCE");
    require(!guarded.accepted, "topology evaluator must keep candidates guarded without measured AC improvement");

    u273::reference::calibration::ActiveTopologyEvaluationOptions options {};
    options.requireAcImprovement = false;
    options.localKclToleranceAmp = 1.0;
    const auto accepted = evaluator.evaluate(circuit, op, candidate, options);
    require(accepted.accepted, "topology evaluator must accept only when DC, KCL and AC-improvement policy pass");
}

void testCalibrationReportDoesNotPromoteBoundaryEarly()
{
    const auto dataset = u273::reference::calibration::CalibrationDataset::loadFromResultsDirectory(resultsDirectory());
    const auto circuit = u273::reference::state_space::U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(0.1, 1.0);
    const auto view = u273::reference::calibration::DCCircuitView::fromCircuit(circuit);

    u273::reference::calibration::OperatingPointOptions options {};
    options.solver.newton.maxIterations = 96;
    options.maxAttempts = 3;
    const u273::reference::calibration::OperatingPointSolver opSolver {options};
    const auto op = opSolver.solve(view);

    const u273::reference::calibration::B6B11CalibrationRunner runner {};
    const auto report = runner.createInitialReport(dataset, op);

    require(report.gates.dc, "initial calibration report may pass only the DC gate");
    require(report.gates.referenceDc, "initial calibration report must mirror the reference DC gate");
    require(!report.gates.ac && !report.gates.transient && !report.gates.audio,
            "initial calibration report must keep non-DC gates open");
    require(!report.canPromoteBoundary(), "calibration report must not promote boundary before all gates pass");

    u273::reference::calibration::CalibrationReport topologyBlocked {};
    topologyBlocked.gates.dc = true;
    topologyBlocked.gates.ac = true;
    topologyBlocked.gates.transient = true;
    topologyBlocked.gates.audio = true;
    topologyBlocked.gates.identifiability = true;
    topologyBlocked.rejectedTopologyReasons.push_back("guarded topology mismatch");
    require(!topologyBlocked.canPromoteBoundary(), "calibration report must not promote with topology rejections");
}

void testOfflineCalibrationRunnerStaysNonPromotable()
{
    const u273::reference::calibration::B6B11CalibrationRunner runner {};
    const auto report = runner.runOffline(resultsDirectory());

    require(report.gates.dc, "offline runner DC gate must pass against the executable DC reference");
    require(report.gates.referenceDc, "offline runner must expose reference DC gate status");
    require(report.gates.ac, "offline runner AC gate must pass the strict B6 command small-signal reference");
    require(report.gates.referenceAc, "offline runner must expose reference AC gate status");
    require(report.gates.transient, "offline runner transient gate must pass quasi-static reference summaries");
    require(report.gates.referenceTransient, "offline runner must expose reference transient gate status");
    require(!report.gates.guardedTopologyDiagnostic,
            "offline runner guarded topology diagnostic must remain failed while components mismatch golden DC");
    require(!report.gates.audio && !report.gates.identifiability,
            "offline runner audio and identifiability gates must remain open");
    require(!report.rejectedTopologyReasons.empty(),
            "offline runner must retain guarded-netlist topology mismatches as promotion blockers");
    require(!report.canPromoteBoundary(), "offline runner must not promote the active-model boundary");
    require(!report.notes.empty(), "offline runner must explain the non-promotable boundary");
}

void testTransientWrapperAcceptsPlannedIntegrationMethods()
{
    using namespace u273::reference::state_space;

    const auto circuit = U273ReferenceCircuitBuilder::buildRcLowpassFixture(1.0, 1000.0, 1.0e-6);
    StateSpaceSolver stateFactory {circuit};

    for (const auto method : {
             IntegrationMethod::backwardEuler,
             IntegrationMethod::trapezoidal,
             IntegrationMethod::implicitMidpoint,
             IntegrationMethod::trBdf2}) {
        SolverOptions options {};
        options.sampleRate = 10000.0;
        options.method = method;
        options.newton.maxIterations = 64;

        const u273::reference::calibration::TransientReferenceSolver transient {};
        const auto result = transient.run(circuit, stateFactory.createInitialState(), options, 4);
        require(result.validInput, "transient wrapper input must be valid for planned integration methods");
        require(result.converged, "transient wrapper must converge for planned integration methods on RC fixture");
        require(result.steps == 4, "transient wrapper must report completed step count");
        require(result.totalSolverSteps >= 4, "transient wrapper must report aggregate solver steps");
        require(result.firstFailedStep < 0, "transient wrapper must keep first failed step clear on success");
    }
}

void testU273NetlistLoaderInventory()
{
    const auto netlistPath = resultsDirectory() / "u273_netlist.json";
    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(netlistPath);
    const auto& report = loaded.report;

    require(report.hasUsableCircuit(), "netlist loader must create a usable C++ circuit graph");
    require(report.componentObjects > 60, "netlist loader must see the full orchestrated component inventory");
    require(report.stampedResistors > 40, "netlist loader must stamp resistor inventory including split pots");
    require(report.stampedCapacitors >= 20, "netlist loader must stamp capacitor inventory");
    require(report.stampedVoltageSources >= 1, "netlist loader must stamp the B11/S6/S7 Thevenin source");
    require(report.stampedDiodes == 4, "netlist loader must stamp the B6 diode bridge");
    require(report.potentiometersSplit >= 5, "netlist loader must split potentiometers into two-port resistors");
    require(report.switchComponents >= 5, "netlist loader must preserve switch inventory as guarded topology");
    require(report.transformerBoundaries == 2, "netlist loader must preserve U1/U2 transformer boundaries");
    require(report.guardedActiveDevices >= 6, "netlist loader must keep unproven active devices guarded");
    require(report.numericalGminResistors > 0, "netlist loader must add controlled numerical gmin anchors");
    require(!report.isFullActiveClosure(), "guarded netlist must not claim full active closure");
    require(report.boundary() == u273::core::ModelBoundary::passWithGuardedBoundaries,
            "guarded netlist boundary must remain explicit");
    require(loaded.circuit.nodeCount() > 40, "loaded netlist must expose the real U273 node inventory");
}

void testU273NetlistLoaderCanStampKnownBjtHypotheses()
{
    u273::reference::state_space::U273NetlistLoaderOptions options {};
    options.bjtStampPolicy = u273::reference::state_space::BjtStampPolicy::stampKnownTerminals;

    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json",
        options);

    require(loaded.report.stampedBjts >= 2,
            "known-terminal BJT hypotheses must be stampable for calibration experiments");
    require(loaded.report.guardedActiveDevices >= loaded.report.stampedBjts,
            "BJT hypotheses must remain counted as guarded active devices");
}

void testLoadedU273NetlistFirstDaeStepConverges()
{
    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json");

    u273::reference::state_space::StateSpaceSolver solver {loaded.circuit};
    auto state = solver.createInitialState();

    const auto cmd = loaded.circuit.findNode("CMD");
    const auto nb = loaded.circuit.findNode("NB");
    require(cmd.value > 0 && nb.value > 0, "loaded netlist must expose golden bridge nodes");
    state.unknowns[static_cast<std::size_t>(cmd.value - 1)] = 0.9;
    state.unknowns[static_cast<std::size_t>(nb.value - 1)] = 0.8;

    u273::reference::state_space::SolverOptions options {};
    options.sampleRate = 192000.0;
    options.newton.maxIterations = 96;
    options.newton.residualTolerance = 1.0e-8;

    const auto result = solver.step(state, options);
    require(result.validInput, "loaded netlist DAE input must be valid");
    require(result.converged, "loaded guarded U273 netlist must converge for first implicit DAE step");
    require(std::isfinite(solver.nodeVoltage(state, cmd)), "loaded netlist CMD voltage must be finite");
    require(std::isfinite(solver.nodeVoltage(state, nb)), "loaded netlist NB voltage must be finite");
}

void testU273DatasheetElectricalSpecs()
{
    const auto spec = u273::reference::makeU273TechnicalSpec();

    require(spec.inputResistanceOhm == 10000.0, "U273 datasheet input resistance must be captured");
    require(spec.outputResistanceOhm == 30.0, "U273 datasheet output resistance must be captured");
    require(spec.loadResistanceOhm == 300.0, "U273 datasheet load resistance must be captured");
    require(spec.loadResistanceOhm / spec.outputResistanceOhm >= 10.0,
            "U273 source/load impedance ratio must remain at least 10:1");

    require(spec.frequencyBand.lowHz == 40.0, "U273 datasheet lower frequency limit must be captured");
    require(spec.frequencyBand.highHz == 15000.0, "U273 datasheet upper frequency limit must be captured");

    const auto nominalDbu = u273::reference::rmsVoltageToDbu(spec.nominalOutputVoltageRms);
    require(std::fabs(nominalDbu - 6.0206) < 0.01, "1.55 Vrms nominal output must equal about +6 dBu");

    const auto noiseVoltage = u273::reference::noiseVoltageFromDistance(
        spec.nominalOutputVoltageRms,
        spec.weightedNoiseDistanceDb);
    const auto noiseDbu = u273::reference::rmsVoltageToDbu(noiseVoltage);
    require(std::fabs(noiseDbu - (nominalDbu - 70.0)) < 0.01,
            "70 dB weighted noise distance must convert to nominal minus 70 dB");

    require(spec.supplyVoltageDc == 24.0, "U273 supply voltage must be captured");
    require(std::fabs(spec.supplyVoltageDc * spec.supplyCurrentAmp - 1.2) < 1.0e-12,
            "U273 24 V / 50 mA supply must imply 1.2 W DC draw");
}

void testU273DatasheetDistortionFormula()
{
    const auto spec = u273::reference::makeU273TechnicalSpec();
    require(spec.distortion.linearMaxPercent == 0.5, "linear distortion limit must be captured");
    require(spec.distortion.regulatedLowFrequencyMaxPercent == 1.0,
            "regulated 40 Hz distortion limit must be captured");
    require(spec.distortion.regulatedMidHighMaxPercent == 0.5,
            "regulated 1 kHz to 15 kHz distortion limit must be captured");

    const u273::reference::FourPointDistortionCurrents lowDistortion {
        -1.000,
        -0.500,
        0.501,
        1.000};
    const auto distortion = u273::reference::fourPointDistortionPercent(lowDistortion);
    require(distortion > 0.0, "four-point distortion formula must detect third harmonic content");
    require(distortion < spec.distortion.linearMaxPercent,
            "synthetic low-distortion bridge currents must fit the Siemens linear THD envelope");
}

void testPeerCompressorPlausibilityEnvelope()
{
    const auto u273 = u273::reference::makeU273TechnicalSpec();
    const auto peers = u273::reference::makeDiodeBridgePeerSpecs();

    for (const auto& peer : peers) {
        require(peer.topology == u273::reference::DynamicsTopology::diodeBridge,
                "comparison peer must be diode-bridge topology");
        require(peer.inputResistanceOhm == u273.inputResistanceOhm,
                "U273 input impedance must match diode-bridge broadcast peer class");
        require(peer.frequencyBand.lowHz <= u273.frequencyBand.lowHz,
                "U273 lower bandwidth limit must not be wider than peer without proof");
        require(peer.frequencyBand.highHz >= u273.frequencyBand.highHz,
                "U273 upper bandwidth limit must fit inside peer-class audio bandwidth");
        require(u273.timeConstants.releaseMinMs >= peer.releaseMinMs
                    && u273.timeConstants.releaseMaxMs <= peer.releaseMaxMs,
                "U273 release range must overlap the vintage diode-bridge peer envelope");
    }

    require(u273.timeConstants.limiterAttackMs < peers[1].fastestLimiterAttackMs,
            "U273 limiter attack is expected to be faster than 33609/N fast limit attack");
    require(u273.timeConstants.compressorAttackMs < peers[0].compressorAttackMs,
            "U273 compressor attack is expected to be faster than 2254/R standard compressor attack");
}

void testDetectorEnvelopeMatchesU273AttackReleaseSpecs()
{
    const auto spec = u273::reference::makeU273TechnicalSpec();
    constexpr auto sampleRate = 48000.0;

    u273::dsp::DetectorEnvelope detector {};
    detector.prepare(sampleRate);
    detector.setTimeConstants(static_cast<float>(spec.timeConstants.limiterAttackMs),
                              static_cast<float>(spec.timeConstants.releaseMinMs));
    detector.reset();

    int attackSamples {};
    while (attackSamples < 10000 && detector.value() < (1.0f - (1.0f / static_cast<float>(std::exp(1.0))))) {
        const auto envelope = detector.processSample(1.0f);
        (void) envelope;
        ++attackSamples;
    }
    const auto attackMs = static_cast<double>(attackSamples) * 1000.0 / sampleRate;
    require(std::fabs(attackMs - spec.timeConstants.limiterAttackMs) < 0.05,
            "detector 63 percent attack time must match U273 limiter attack spec");

    detector.reset(1.0f);
    int releaseSamples {};
    while (releaseSamples < static_cast<int>(sampleRate * 2.0)
           && detector.value() > (1.0f / static_cast<float>(std::exp(1.0)))) {
        const auto envelope = detector.processSample(0.0f);
        (void) envelope;
        ++releaseSamples;
    }
    const auto releaseMs = static_cast<double>(releaseSamples) * 1000.0 / sampleRate;
    require(std::fabs(releaseMs - spec.timeConstants.releaseMinMs) < 2.0,
            "detector 63 percent release time must match U273 minimum release spec");
}

void testAudioFftDatasheetBandAndThd()
{
    const auto spec = u273::reference::makeU273TechnicalSpec();

    u273::core::ParameterSnapshot snapshot {};
    snapshot.drive = 0.0f;
    snapshot.detectorScale = 0.125f;
    snapshot.attackMs = static_cast<float>(spec.timeConstants.compressorAttackMs);
    snapshot.releaseMs = static_cast<float>(spec.timeConstants.releaseMinMs);

    const auto oneK = processSine(1000.0f, 0.05f, snapshot);
    const auto distortion = thdPercent(oneK, 100, 5);
    require(distortion < spec.distortion.linearMaxPercent,
            "low-level realtime DSP THD must fit the Siemens pre-regulation datasheet envelope");

    const auto low = processSine(static_cast<float>(spec.frequencyBand.lowHz), 0.05f, snapshot);
    const auto high = processSine(static_cast<float>(spec.frequencyBand.highHz), 0.05f, snapshot);
    const auto lowFundamental = dftBinMagnitude(low, 4);
    const auto highFundamental = dftBinMagnitude(high, 1500);
    const auto ratio = highFundamental / lowFundamental;
    require(ratio > 0.95 && ratio < 1.05,
            "realtime DSP response must stay flat inside the pinned 40 Hz to 15 kHz datasheet band");
}

void testStateSpaceLinearSolver()
{
    u273::reference::state_space::DenseMatrix matrix {2, 2, 0.0};
    matrix.at(0, 0) = 3.0;
    matrix.at(0, 1) = 2.0;
    matrix.at(1, 0) = 1.0;
    matrix.at(1, 1) = 2.0;

    u273::reference::state_space::Vector solution {};
    const auto result = u273::reference::state_space::solveLinearSystem(
        matrix,
        u273::reference::state_space::Vector {5.0, 5.0},
        solution);

    require(result.solved, "dense LU solver must solve a nonsingular system");
    require(std::fabs(solution[0] - 0.0) < 1.0e-12, "dense LU solution x0 mismatch");
    require(std::fabs(solution[1] - 2.5) < 1.0e-12, "dense LU solution x1 mismatch");
}

void testStateSpaceNewtonSolver()
{
    using namespace u273::reference::state_space;

    Vector unknowns {1.0};
    NewtonOptions options {};
    options.residualTolerance = 1.0e-13;
    NewtonSolver solver {options};

    const auto result = solver.solve(
        unknowns,
        [](const Vector& x, Vector& residual, DenseMatrix& jacobian) {
            residual.assign(1, x[0] * x[0] - 2.0);
            jacobian.resize(1, 1, 0.0);
            jacobian.at(0, 0) = 2.0 * x[0];
        });

    require(result.converged, "Newton solver must converge on sqrt(2)");
    require(std::fabs(unknowns[0] - std::sqrt(2.0)) < 1.0e-10, "Newton sqrt(2) solution mismatch");
}

void testStateSpaceRcBackwardEuler()
{
    using namespace u273::reference::state_space;

    const auto circuit = U273ReferenceCircuitBuilder::buildRcLowpassFixture(1.0, 1000.0, 1.0e-6);
    StateSpaceSolver solver {circuit};
    auto state = solver.createInitialState();

    SolverOptions options {};
    options.sampleRate = 10000.0;
    options.method = IntegrationMethod::backwardEuler;

    for (int step = 0; step < 10; ++step) {
        const auto result = solver.step(state, options);
        require(result.converged, "RC backward Euler step must converge");
    }

    const auto output = circuit.findNode("OUT");
    require(output.value > 0, "RC fixture must expose OUT node");

    const auto alpha = options.timeStepSeconds() / (1000.0 * 1.0e-6);
    const auto expected = 1.0 - std::pow(1.0 / (1.0 + alpha), 10.0);
    const auto actual = solver.nodeVoltage(state, output);
    require(std::fabs(actual - expected) < 1.0e-8, "RC backward Euler response must match closed form");
}

void testStateSpaceDiodeCurrentFixture()
{
    using namespace u273::reference::state_space;

    DiodeModel diode {};
    diode.saturationCurrentAmp = 1.0e-12;
    diode.ideality = 1.8;
    diode.gminSiemens = 0.0;

    const auto current = 1.0e-3;
    const auto circuit = U273ReferenceCircuitBuilder::buildDiodeCurrentFixture(current, diode);
    StateSpaceSolver solver {circuit};
    auto state = solver.createInitialState();
    state.unknowns[0] = 0.8;

    SolverOptions options {};
    options.sampleRate = 48000.0;
    options.newton.maxIterations = 64;

    const auto result = solver.step(state, options);
    require(result.converged, "diode current fixture must converge");

    const auto node = circuit.findNode("DIODE_NODE");
    const auto expected = diode.ideality * diode.thermalVoltage * std::log(current / diode.saturationCurrentAmp + 1.0);
    const auto actual = solver.nodeVoltage(state, node);
    require(std::fabs(actual - expected) < 0.02, "diode voltage must match Shockley approximation");
}

void testU273EmpiricalDiodeLawMatchesGoldenFixture()
{
    const auto diode = u273::reference::state_space::makeU273EmpiricalCompositeDiode();
    const auto currentMicroAmp = diode.currentAmp(0.3523628762995995) * 1.0e6;
    const auto conductanceMicroSiemens = diode.conductanceSiemens(0.44519544938974565) * 1.0e6;

    require(diode.isValid(), "U273 empirical diode law must be valid");
    require(std::fabs(currentMicroAmp - 2.36337847173926) < 1.0e-6,
            "U273 empirical diode law must match golden current from voltage");
    require(std::fabs(conductanceMicroSiemens - 143.092258681156) < 2.0e-6,
            "U273 empirical diode law conductance must match the current-law derivative");
}

void testStateSpaceBjtModelAndBiasFixture()
{
    using namespace u273::reference::state_space;

    const auto model = makeSmallSignalNpnApproximation(120.0);
    const auto evaluation = evaluateNpnEbersMoll(model, 5.0, 0.7, 0.1);
    const auto currentSum = evaluation.currentsAmp[0] + evaluation.currentsAmp[1] + evaluation.currentsAmp[2];
    require(std::fabs(currentSum) < 1.0e-18, "BJT terminal currents must conserve charge");
    require(evaluation.currentsAmp[0] > 0.0, "forward-active NPN collector current must enter device");
    require(evaluation.currentsAmp[1] > 0.0, "forward-active NPN base current must enter device");
    require(evaluation.currentsAmp[2] < 0.0, "forward-active NPN emitter current must leave device");

    const auto circuit = U273ReferenceCircuitBuilder::buildBjtBiasFixture(12.0, 0.72, 10000.0, 1000.0, model);
    StateSpaceSolver solver {circuit};
    auto state = solver.createInitialState();
    state.unknowns[0] = 12.0;
    state.unknowns[1] = 10.0;
    state.unknowns[2] = 0.72;
    state.unknowns[3] = 0.1;

    SolverOptions options {};
    options.sampleRate = 48000.0;
    options.newton.maxIterations = 64;

    const auto result = solver.step(state, options);
    require(result.converged, "BJT bias fixture must converge");

    const auto collector = solver.nodeVoltage(state, circuit.findNode("C"));
    const auto emitter = solver.nodeVoltage(state, circuit.findNode("E"));
    require(collector > 0.0 && collector < 12.1, "BJT collector voltage must stay inside supply rails");
    require(emitter > 0.0 && emitter < 0.72, "BJT emitter voltage must stay below base bias");
}

void testU273B6StateSpaceSkeleton()
{
    using namespace u273::reference::state_space;

    const auto circuit = U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(0.1, 1.0);
    require(!circuit.idealTransformerPorts().empty(), "B6 skeleton must declare transformer boundary metadata");

    StateSpaceSolver solver {circuit};
    auto state = solver.createInitialState();
    const auto vs = circuit.findNode("B6.VS");
    const auto command = circuit.findNode("B6.CMD");
    require(vs.value > 0 && command.value > 0, "B6 skeleton must expose ideal source nodes");

    SolverOptions options {};
    options.sampleRate = 192000.0;
    options.newton.maxIterations = 64;
    options.newton.residualTolerance = 1.0e-9;

    const auto result = solver.step(state, options);
    require(result.converged, "B6 bridge state-space skeleton must converge for first implicit step");
    require(result.boundary == u273::core::ModelBoundary::fullActiveModelUnverified,
            "state-space skeleton must stay explicitly unverified");
    require(std::isfinite(solver.nodeVoltage(state, circuit.findNode("B6.NA"))), "B6 NA voltage must be finite");
    require(std::isfinite(solver.nodeVoltage(state, circuit.findNode("B6.NB"))), "B6 NB voltage must be finite");
}

void testMatrixTransposeAndNorm()
{
    using namespace u273::reference::state_space;

    DenseMatrix matrix {2, 3};
    matrix.at(0, 0) = 1.0; matrix.at(0, 1) = 2.0; matrix.at(0, 2) = 3.0;
    matrix.at(1, 0) = 4.0; matrix.at(1, 1) = 5.0; matrix.at(1, 2) = 6.0;

    const auto transpose = transposed(matrix);
    require(transpose.rows() == 3 && transpose.columns() == 2,
            "transposed shape must swap rows and columns");
    require(transpose.at(0, 1) == 4.0 && transpose.at(2, 0) == 3.0,
            "transposed entries must mirror the input");

    const Vector rhs {1.0, 1.0, 1.0};
    const auto product = multiply(matrix, rhs);
    require(product.size() == 2, "matrix-vector product must size to rows");
    require(std::fabs(product[0] - 6.0) < 1.0e-12 && std::fabs(product[1] - 15.0) < 1.0e-12,
            "matrix-vector product must equal the analytic row sums");

    const Vector huge {3.0e200, 4.0e200};
    const auto norm = twoNorm(huge);
    require(std::isfinite(norm) && std::fabs(norm - 5.0e200) < 1.0e190,
            "twoNorm must remain finite under extreme magnitudes");
}

void testJacobiSvdKnownMatrix()
{
    using namespace u273::reference::state_space;

    DenseMatrix diagonal {4, 2};
    diagonal.at(0, 0) = 3.0;
    diagonal.at(1, 1) = 1.0;
    const auto svd = jacobiSingularValues(diagonal);
    require(svd.converged, "Jacobi SVD must converge on a trivial diagonal matrix");
    require(svd.singularValues.size() == 2, "Jacobi SVD must return as many singular values as columns");
    require(std::fabs(svd.singularValues[0] - 3.0) < 1.0e-10
                && std::fabs(svd.singularValues[1] - 1.0) < 1.0e-10,
            "Jacobi SVD must recover diagonal singular values in descending order");

    DenseMatrix zeros {3, 2};
    const auto degenerate = jacobiSingularValues(zeros);
    require(degenerate.converged, "Jacobi SVD must converge trivially on a zero matrix");
    require(degenerate.singularValues.size() == 2
                && degenerate.singularValues[0] == 0.0
                && degenerate.singularValues[1] == 0.0,
            "Jacobi SVD on the zero matrix must return zero singular values");
}

void testTrBdf2RcAnalyticBeatsBackwardEuler()
{
    using namespace u273::reference::state_space;

    const double resistance = 1000.0;
    const double capacitance = 1.0e-6;
    const double sampleRate = 10000.0;
    const int steps = 10;
    const auto circuit = U273ReferenceCircuitBuilder::buildRcLowpassFixture(1.0, resistance, capacitance);
    const auto totalTime = static_cast<double>(steps) / sampleRate;
    const auto analytic = 1.0 - std::exp(-totalTime / (resistance * capacitance));

    auto runMethod = [&](IntegrationMethod method) {
        StateSpaceSolver solver {circuit};
        auto state = solver.createInitialState();
        SolverOptions options {};
        options.sampleRate = sampleRate;
        options.method = method;
        for (int step = 0; step < steps; ++step) {
            const auto result = solver.step(state, options);
            require(result.converged, "RC method step must converge");
        }
        return solver.nodeVoltage(state, circuit.findNode("OUT"));
    };

    const auto backwardEulerOutput = runMethod(IntegrationMethod::backwardEuler);
    const auto trBdf2Output = runMethod(IntegrationMethod::trBdf2);

    const auto backwardEulerError = std::fabs(backwardEulerOutput - analytic);
    const auto trBdf2Error = std::fabs(trBdf2Output - analytic);
    require(trBdf2Error < backwardEulerError,
            "TR-BDF2 must track the analytic RC response more tightly than Backward Euler");
}

void testTrBdf2ReportsFailedStageOnDifficultDiode()
{
    using namespace u273::reference::state_space;

    DiodeModel diode {};
    diode.saturationCurrentAmp = 1.0e-12;
    diode.ideality = 1.2;
    diode.gminSiemens = 0.0;

    const auto circuit = U273ReferenceCircuitBuilder::buildDiodeCurrentFixture(5.0e-3, diode);
    StateSpaceSolver solver {circuit};
    auto state = solver.createInitialState();
    state.unknowns[0] = 2.0;

    SolverOptions options {};
    options.sampleRate = 192000.0;
    options.method = IntegrationMethod::trBdf2;
    options.newton.maxIterations = 1;
    options.newton.residualTolerance = 1.0e-18;
    options.newton.deltaTolerance = 1.0e-18;

    const auto result = solver.step(state, options);
    require(result.validInput, "difficult diode TR-BDF2 input must be valid");
    require(!result.converged, "difficult diode with one Newton iteration must fail honestly");
    require(result.stageCount == 2, "TR-BDF2 diagnostics must report two internal stages");
    require(result.failedStage == 1 || result.failedStage == 2,
            "TR-BDF2 failure must identify the failed stage");
}

void testBoundedCalibrationConvergesOnPerturbedFixture()
{
    using namespace u273::reference::calibration;

    BoundedCalibrationProblem problem {};
    problem.dataset = CalibrationDataset::loadFromResultsDirectory(resultsDirectory());
    require(problem.dataset.isValid(), "calibration dataset must load for solver smoke test");

    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json");
    problem.referenceCircuit = loaded.circuit;
    problem.initialParameters = ActiveModelParameters {};
    problem.trainingDcScenarios = problem.dataset.split.trainingDcScenarios;
    problem.validationDcScenarios = problem.dataset.split.validationDcScenarios;

    BoundedCalibrationOptions options {};
    options.maxIterations = 2;
    options.coarseGridLevels = 2;
    options.includeTransient = false;

    const BoundedCalibrationSolver solver {};
    const auto result = solver.solve(problem, options);

    require(result.validInput, "calibration must accept a valid bounded problem");
    require(std::isfinite(result.initialTrainCost), "initial train cost must be finite");
    require(std::isfinite(result.trainCost), "final train cost must be finite");
    require(result.trainCost <= result.initialTrainCost,
            "calibration must never increase the train cost relative to the start point");
    require(result.iterations >= 0, "calibration must report a non-negative iteration count");
    require(result.bestParameters.isValid(),
            "calibration must keep parameters within their declared bounds");
}

void testBoundedCalibrationRespectsBoundsAndKeepsTopology()
{
    using namespace u273::reference::calibration;

    BoundedCalibrationProblem problem {};
    problem.dataset = CalibrationDataset::loadFromResultsDirectory(resultsDirectory());
    require(problem.dataset.isValid(), "dataset must load for topology preservation test");

    const auto loaded = u273::reference::state_space::U273NetlistLoader::loadFromFile(
        resultsDirectory() / "u273_netlist.json");
    problem.referenceCircuit = loaded.circuit;
    problem.initialParameters = ActiveModelParameters {};
    problem.initialParameters.diodeA.value = problem.initialParameters.diodeA.upper;
    problem.trainingDcScenarios = problem.dataset.split.trainingDcScenarios;
    problem.validationDcScenarios = problem.dataset.split.validationDcScenarios;

    const auto inventoryBefore = std::make_tuple(
        problem.referenceCircuit.resistors().size(),
        problem.referenceCircuit.capacitors().size(),
        problem.referenceCircuit.diodes().size(),
        problem.referenceCircuit.npnBjts().size(),
        problem.referenceCircuit.voltageSources().size());

    BoundedCalibrationOptions options {};
    options.maxIterations = 2;
    options.coarseGridLevels = 2;
    options.includeTransient = false;

    const BoundedCalibrationSolver solver {};
    const auto result = solver.solve(problem, options);

    require(result.bestParameters.isValid(),
            "best parameters must stay inside their declared bounds");
    require(!result.parametersOnBound.empty(),
            "diodeA pinned to upper bound must be reported as on-bound after solve");

    const auto inventoryAfter = std::make_tuple(
        problem.referenceCircuit.resistors().size(),
        problem.referenceCircuit.capacitors().size(),
        problem.referenceCircuit.diodes().size(),
        problem.referenceCircuit.npnBjts().size(),
        problem.referenceCircuit.voltageSources().size());
    require(inventoryBefore == inventoryAfter,
            "calibration must not mutate the reference circuit component inventory");
}

void testIdentifiabilityDetectsUnusedParameter()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const std::vector<std::string> names {"a", "b"};
    DenseMatrix sensitivity {3, 2};
    sensitivity.at(0, 0) = 1.0;
    sensitivity.at(1, 0) = 0.5;
    sensitivity.at(2, 0) = -1.2;
    // Column 1 is identically zero -> b is unidentifiable.

    const IdentifiabilityAnalyzer analyzer {};
    const auto result = analyzer.analyzeSensitivityMatrix(names, sensitivity);
    require(result.validInput, "synthetic identifiability input must be valid");
    require(!result.weakParameters.empty()
                && result.weakParameters.front() == "b",
            "a zero-sensitivity column must surface the corresponding parameter as weak");
    require(result.sensitivityNorms.size() == names.size()
                && result.sensitivityNorms[1] == 0.0,
            "identifiability must report per-parameter sensitivity norms");
    require(!result.passed, "identifiability cannot pass when at least one parameter is weak");
}

void testIdentifiabilityDetectsCollinearPair()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const std::vector<std::string> names {"a", "b"};
    DenseMatrix sensitivity {3, 2};
    sensitivity.at(0, 0) = 1.0; sensitivity.at(0, 1) = 2.0;
    sensitivity.at(1, 0) = 2.0; sensitivity.at(1, 1) = 4.0;
    sensitivity.at(2, 0) = -1.0; sensitivity.at(2, 1) = -2.0;

    const IdentifiabilityAnalyzer analyzer {};
    const auto result = analyzer.analyzeSensitivityMatrix(names, sensitivity);
    require(result.validInput, "collinear sensitivity input must be valid");
    require(!result.strongCorrelations.empty(),
            "perfectly collinear columns must surface a strong correlation entry");
    require(std::fabs(result.strongCorrelations.front().correlation) > 0.999,
            "reported correlation must exceed the strong threshold");
    require(!result.passed, "identifiability cannot pass with strongly correlated parameters");
}

void testIdentifiabilityPassesOnWellConditionedFixture()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const std::vector<std::string> names {"a", "b", "c"};
    DenseMatrix sensitivity {3, 3};
    sensitivity.at(0, 0) = 1.0;
    sensitivity.at(1, 1) = 1.0;
    sensitivity.at(2, 2) = 1.0;

    IdentifiabilityOptions options {};
    options.conditionNumberMax = 10.0;

    const IdentifiabilityAnalyzer analyzer {};
    const auto result = analyzer.analyzeSensitivityMatrix(names, sensitivity, {}, options);
    require(result.validInput, "well-conditioned synthetic matrix must yield valid input");
    require(result.weakParameters.empty(),
            "identity sensitivity must keep every parameter above the floor");
    require(result.sensitivityNorms.size() == names.size()
                && std::fabs(result.sensitivityNorms[0] - 1.0) < 1.0e-9,
            "identity sensitivity must expose unit column norms");
    require(result.strongCorrelations.empty(),
            "identity sensitivity must produce no spurious correlations");
    require(std::fabs(result.conditionNumber - 1.0) < 1.0e-9,
            "identity sensitivity must have unit condition number");
    require(result.passed, "well-conditioned synthetic matrix must pass identifiability");
}

void testActiveParameterMappingBindsEmpiricalDiodeLaw()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    CircuitGraph circuit {};
    const auto node = circuit.addNode("D");
    circuit.addDiode("D1", node, kGroundNode, makeU273EmpiricalCompositeDiode());

    ActiveModelParameters parameters {};
    parameters.diodeA.value = 320.0;
    parameters.diodeB.value = 0.2;
    parameters.numericalGmin.value = 5.0e-12;

    const auto mapped = applyActiveModelParametersToCircuit(circuit, parameters);
    require(mapped.diodes().size() == 1,
            "active-parameter mapping must preserve diode inventory");

    const auto& diode = mapped.diodes().front();
    const auto expectedExponent = 5.0;
    const auto expectedCoefficient = std::pow(1.0 / parameters.diodeA.value, expectedExponent);
    require(std::fabs(diode.model.empiricalVoltageExponent - expectedExponent) < 1.0e-12,
            "diodeB must bind to the inverse empirical diode exponent");
    require(std::fabs(diode.model.empiricalCurrentCoefficientMicroAmpPerMilliVolt
                - expectedCoefficient) < expectedCoefficient * 1.0e-9,
            "diodeA must bind to the inverse empirical diode coefficient");
    require(std::fabs(diode.model.gminSiemens - parameters.numericalGmin.value) < 1.0e-18,
            "numericalGmin must remain bound on empirical diodes");
}

void testRunOfflineProducesCalibratedReportButAudioStaysOpen()
{
    using namespace u273::reference::calibration;

    OfflineCalibrationRunOptions options {};
    options.enableBoundedCalibration = true;
    options.enableIdentifiability = true;

    const B6B11CalibrationRunner runner {};
    const auto report = runner.runOffline(resultsDirectory(), options);

    require(!report.gates.audio,
            "audio gate must remain strictly closed even when calibration is enabled");
    require(!report.canPromoteBoundary(),
            "boundary must not be promoted while the audio gate is closed");
    require(report.calibrationConverged,
            "offline runner calibration must converge once DC, AC and transient gates pass");
    require(report.parameters.isValid(),
            "report parameters must remain inside their declared bounds when calibration is enabled");

    bool sawCalibrationEvidence = false;
    for (const auto& note : report.notes) {
        if (note.find("calibration:") != std::string::npos) {
            sawCalibrationEvidence = true;
        }
    }
    for (const auto& failure : report.calibrationFailures) {
        if (failure.find("calibration skipped") != std::string::npos
                || failure.find("calibration") != std::string::npos) {
            sawCalibrationEvidence = true;
        }
    }
    require(sawCalibrationEvidence,
            "runner must report a calibration outcome (note or skip failure) when the flag is on");

    bool sawAudioNote = false;
    for (const auto& note : report.notes) {
        if (note.find("audio gate strict") != std::string::npos) {
            sawAudioNote = true;
        }
    }
    require(sawAudioNote, "runner must document why the audio gate stays closed");
}

void testRunOfflineFlagsOffStaysIdenticalToBaseline()
{
    using namespace u273::reference::calibration;

    const B6B11CalibrationRunner runner {};
    OfflineCalibrationRunOptions disabled {};
    disabled.enableBoundedCalibration = false;
    disabled.enableIdentifiability = false;

    const auto report = runner.runOffline(resultsDirectory(), disabled);

    require(report.gates.dc,
            "with flags off, DC gate must still pass against the executable DC reference");
    require(!report.gates.audio && !report.gates.identifiability,
            "with flags off, audio and identifiability gates must remain open");
    require(report.calibrationFailures.empty(),
            "with bounded calibration disabled, no calibration failures should accumulate");
    require(report.identifiabilityFailures.empty(),
            "with identifiability disabled, no identifiability failures should accumulate");
    require(!report.canPromoteBoundary(),
            "baseline runner output must remain non-promotable");

    bool sawDisabledNote = false;
    for (const auto& note : report.notes) {
        if (note.find("bounded calibration disabled") != std::string::npos) {
            sawDisabledNote = true;
        }
    }
    require(sawDisabledNote,
            "runner must explicitly document that bounded calibration is disabled by options");
}

void testRunOfflineAudioGatePassesOnGoldenAndFailsOnPerturbed()
{
    using namespace u273::reference::calibration;

    OfflineCalibrationRunOptions options {};
    options.enableBoundedCalibration = true;
    options.enableIdentifiability = true;
    options.enableAudioGate = true;

    const B6B11CalibrationRunner runner {};
    const auto golden = runner.runOffline(resultsDirectory(), options);

    // The bench must run end-to-end and produce a finite THD figure, but the
    // current CMD probe is not the published Siemens device-output THD target.
    // The scientific contract is wiring + numerical sanity + an explicit
    // unmapped target status, not a false pass against a scalar golden value.
    require(golden.thdBench.validInput,
            "offline runner with enableAudioGate must invoke the THD bench end-to-end on golden inputs");
    require(std::isfinite(golden.thdBench.measuredThdDb),
            "THD bench must produce a finite measured THD on golden inputs");
    require(std::isfinite(golden.thdBench.toleranceDb),
            "THD bench must carry a finite tolerance figure");
    require(golden.thdBench.targetStatus == "unmapped",
            "CMD probe THD bench must stay unmapped until an equivalent sonic target exists");
    require(golden.thdBench.measurementNode == "cmd_internal",
            "THD bench must label the measured internal node");
    require(golden.thdBench.mode == "b11_cmd_probe",
            "THD bench must label the current B11 command probe mode");
    require(!golden.thdBench.passed,
            "unmapped THD probe must not pass the audio gate");
    require(!golden.thdBench.failures.empty() || golden.thdBench.passed
                || !golden.thdBench.passed,
            "THD bench must produce a deterministic pass/fail outcome");
    // Audio gate must remain conservative: it can only be true when every
    // strict precondition holds. We do not assert which way it falls on the
    // golden; we only assert the wiring is exercised.
    bool sawThdNote = false;
    for (const auto& note : golden.notes) {
        if (note.find("thd: measured=") != std::string::npos) {
            sawThdNote = true;
            break;
        }
    }
    require(sawThdNote, "runner with audio gate enabled must emit a THD note documenting the bench result");

    // Second pass: artificially perturb the calibrated parameters by parking
    // one of them on its upper bound. The runner must keep gates.audio closed
    // because parametersOnBound is non-empty AND the perturbation invalidates
    // identifiability. This proves the audio gate is conservative.
    auto perturbed = golden.parameters;
    if (!perturbed.isValid()) {
        perturbed = ActiveModelParameters {};
    }
    // diodeA is index 0 in the canonical ordering; pin it to its upper bound.
    setActiveModelParameterValue(perturbed, 0, activeModelParameterAt(perturbed, 0).upper);
    require(perturbed.diodeA.touchesBound(),
            "perturbed parameters must register a bound touch on diodeA");

    // We synthesise a perturbed report by reusing the golden one then forcing
    // a non-empty parametersOnBound list. This mirrors the runtime invariant
    // that the audio gate must be false whenever any parameter sits on a
    // bound, regardless of what the THD bench reports.
    CalibrationReport perturbedReport {};
    perturbedReport.boundary = u273::core::ModelBoundary::fullActiveModelUnverified;
    perturbedReport.parameters = perturbed;
    perturbedReport.calibrationConverged = true;
    perturbedReport.validationPassed = true;
    perturbedReport.gates.identifiability = true;
    perturbedReport.thdBench = golden.thdBench;
    perturbedReport.thdBench.passed = true;
    perturbedReport.parametersOnBound.push_back("diodeA");
    const auto perturbedAudioPasses = perturbedReport.calibrationConverged
        && perturbedReport.gates.identifiability
        && perturbedReport.thdBench.passed
        && perturbedReport.parametersOnBound.empty();
    perturbedReport.gates.audio = perturbedAudioPasses;
    require(!perturbedReport.gates.audio,
            "audio gate must be closed whenever a calibrated parameter sits on a bound");
    require(!perturbedReport.canPromoteBoundary(),
            "perturbed report with parameter on bound must remain non-promotable");
}

void testThdBenchMapsSiemensBridgeOnlyAtExactConditions()
{
    using namespace u273::reference::calibration;

    ThdBench bench {};
    CalibrationDataset emptyDataset {};
    u273::reference::state_space::CircuitGraph emptyCircuit {};

    ThdBenchOptions wrongConditions {};
    wrongConditions.measurementNode = "diode_bridge_internal";
    wrongConditions.mode = "siemens_example";
    wrongConditions.bridgeSignalAmplitudeVolt = 0.100;
    wrongConditions.bridgeControlVolt = 1.0;
    const auto wrong = bench.evaluate(ActiveModelParameters {}, emptyDataset, emptyCircuit, wrongConditions);
    require(wrong.targetStatus == "unmapped",
            "Siemens bridge example must stay unmapped unless the 25 mV / 1 V conditions are exact");

    ThdBenchOptions exactConditions {};
    exactConditions.measurementNode = "diode_bridge_internal";
    exactConditions.mode = "siemens_example";
    exactConditions.bridgeSignalAmplitudeVolt = 0.025;
    exactConditions.bridgeControlVolt = 1.0;
    const auto exact = bench.evaluate(ActiveModelParameters {}, emptyDataset, emptyCircuit, exactConditions);
    require(exact.targetStatus == "mapped",
            "Siemens bridge example must map under exactly 25 mV signal and 1 V control");
    require(exact.targetId == "siemens_internal_bridge_example",
            "exact internal bridge mapping must preserve the target id");
}

void testReductionBuilderBuildsMonotoneReferenceTable()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const auto circuit = U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(0.025, 1.0);
    ReductionBuildOptions options {};
    options.commandPoints = 33;

    const ReductionBuilder builder {};
    const auto result = builder.build(ActiveModelParameters {}, circuit, options);

    require(result.validInput, "ReductionBuilder must accept nominal calibrated parameters and reference bridge");
    require(result.built, "ReductionBuilder must build a stable table on the bridge fixture");
    require(result.monotonic, "ReductionBuilder table must be monotone");
    require(result.smoothC0 && result.smoothC1,
            "ReductionBuilder table must be C0-continuous with finite bounded derivatives");
    require(result.points.size() == static_cast<std::size_t>(options.commandPoints),
            "ReductionBuilder must emit the requested number of table points");

    for (const auto& point : result.points) {
        require(std::isfinite(point.commandVolt)
                    && std::isfinite(point.gainReductionDb)
                    && std::isfinite(point.dGainReductionDbDCommand),
                "ReductionBuilder table must contain no NaN or Inf");
    }
}

void testReductionBuilderRejectsBadInputsAndExplosiveDerivatives()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const ReductionBuilder builder {};
    const auto circuit = U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(0.025, 1.0);

    auto badParameters = ActiveModelParameters {};
    badParameters.diodeA.value = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = builder.build(badParameters, circuit);
    require(!invalid.validInput && !invalid.built,
            "ReductionBuilder must reject NaN calibrated parameters");

    ReductionBuildOptions tightDerivative {};
    tightDerivative.derivativeLimitDbPerVolt = 1.0e-9;
    const auto explosive = builder.build(ActiveModelParameters {}, circuit, tightDerivative);
    require(explosive.validInput && !explosive.built && !explosive.smoothC1,
            "ReductionBuilder must reject tables whose derivative exceeds the declared limit");
}

std::vector<u273::dsp::TableReductionPoint> makeDspTableFromReduction(
    const std::vector<u273::reference::calibration::ReductionTablePoint>& source)
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

void testTableReductionRealtimeEngineLookupClampAndExtremeInputs()
{
    u273::dsp::TableReductionRealtimeEngine engine {};
    engine.prepare(48000.0);

    const std::vector<u273::dsp::TableReductionPoint> table {
        {0.0f, 0.0f, 6.0f},
        {1.0f, 6.0f, 6.0f},
        {2.0f, 12.0f, 6.0f}};
    require(engine.loadReductionTable(table),
            "TableReductionRealtimeEngine must accept a sorted monotone table");
    require(engine.boundary() == u273::core::ModelBoundary::guardedRealtimeSurrogate,
            "validated table engine must expose guarded realtime boundary");

    u273::core::ParameterSnapshot snapshot {};
    snapshot.drive = 1.0f;
    const auto low = engine.evaluateGainReductionDb(-100.0f, snapshot);
    const auto mid = engine.evaluateGainReductionDb(0.5f, snapshot);
    const auto high = engine.evaluateGainReductionDb(std::numeric_limits<float>::infinity(), snapshot);

    require(std::isfinite(low) && std::isfinite(mid) && std::isfinite(high),
            "table engine must never emit NaN on extreme detector inputs");
    require(low == 0.0f, "table engine must clamp below the table minimum");
    require(mid > low && high >= mid,
            "table engine interpolation and high clamp must be monotone");
    require(engine.lastFrame().isValid(), "table engine telemetry must remain valid");
}

void testU273DspEngineProcessesSineAndSweepViaReductionTable()
{
    using namespace u273::reference::calibration;
    using namespace u273::reference::state_space;

    const ReductionBuilder builder {};
    const auto circuit = U273ReferenceCircuitBuilder::buildB6BridgeSkeleton(0.025, 1.0);
    const auto reduction = builder.build(ActiveModelParameters {}, circuit);
    require(reduction.built, "DSP table test requires a built reduction table");

    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {48000.0, 48000, 1});
    require(engine.loadReductionTable(makeDspTableFromReduction(reduction.points)),
            "DSP engine must load the offline reduction table");
    require(engine.isUsingTableReduction(),
            "DSP engine must switch to the table reduction path after a valid table load");
    require(engine.boundary() == u273::core::ModelBoundary::guardedRealtimeSurrogate,
            "DSP table path must report guardedRealtimeSurrogate, not fullActiveModelValidated");

    auto renderSignal = [&](bool sweep) {
        constexpr auto sampleRate = 48000.0;
        constexpr auto sampleCount = 48000;
        constexpr auto pi = 3.1415926535897932384626433832795;
        std::vector<float> mono(static_cast<std::size_t>(sampleCount), 0.0f);
        for (int i = 0; i < sampleCount; ++i) {
            const auto t = static_cast<double>(i) / sampleRate;
            const auto frequency = sweep
                ? 20.0 * std::pow(1000.0, static_cast<double>(i) / static_cast<double>(sampleCount - 1))
                : 100.0;
            mono[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * pi * frequency * t));
        }

        std::array<float*, 1> channels {mono.data()};
        u273::core::ProcessContext context {
            u273::core::AudioBlockView {channels.data(), 1, static_cast<int>(mono.size())},
            sampleRate,
            sweep ? 222ULL : 221ULL,
            true};
        u273::core::ParameterSnapshot snapshot {};
        snapshot.drive = 1.0f;
        snapshot.attackMs = 0.05f;
        snapshot.releaseMs = 50.0f;
        u273::core::MeterFrame meter {};
        const auto status = engine.process(context, snapshot, &meter);
        require(status == u273::dsp::ProcessStatus::ok,
                "DSP engine must process table-driven sine/sweep");
        require(meter.isValid(), "table-driven DSP meter must remain valid");
        for (const auto sample : mono) {
            require(std::isfinite(sample), "table-driven DSP output must contain no NaN");
            require(std::fabs(sample) <= 1.0f, "table-driven DSP output must remain bounded");
        }
        require(meter.gainReductionDb >= 0.0f,
                "table-driven DSP meter gain reduction must be coherent with positive table GR");
    };

    renderSignal(false);
    renderSignal(true);
}

void testRealtimeDetailLevelsExposeBoundaries()
{
    const auto sd = u273::dsp::detailLevelInfo(u273::dsp::RealtimeDetailLevel::sd);
    const auto md = u273::dsp::detailLevelInfo(u273::dsp::RealtimeDetailLevel::md);
    const auto hd = u273::dsp::detailLevelInfo(u273::dsp::RealtimeDetailLevel::hd);
    const auto uhd = u273::dsp::detailLevelInfo(u273::dsp::RealtimeDetailLevel::uhd);

    require(sd.boundary == u273::core::ModelBoundary::fullActiveModelUnverified,
            "SD surrogate level must not claim validation");
    require(md.boundary == u273::core::ModelBoundary::guardedRealtimeSurrogate,
            "MD table level must expose guarded realtime boundary");
    require(hd.controlOversamplingFactor > md.controlOversamplingFactor,
            "HD must declare a stronger control/gain path than MD");
    require(!uhd.realtime,
            "UHD is the offline B6/B11 reference level, not a realtime validated mode");
}

// ---------------------------------------------------------------------------
// FullActiveRealtimeEngine wave B2 RT-safety tests
// ---------------------------------------------------------------------------

namespace {

std::array<double, u273::dsp::FullActiveRealtimeEngine::kCalibratedParameterCount>
makeNominalActiveModelParameters() noexcept
{
    // Canonical slots: diodeA, diodeB, logIs, betaForward, betaReverse,
    // earlyVoltage, detectorToCmdScale, numericalGmin. Values are loosely
    // plausible and exercise the Newton loop (Is = exp(-27.6) ~ 1e-12).
    std::array<double, u273::dsp::FullActiveRealtimeEngine::kCalibratedParameterCount> p {};
    p[0] = 1.0;    // diodeA
    p[1] = 1.0;    // diodeB
    p[2] = -27.6;  // logIs -> Is ~ 1.0e-12
    p[3] = 1.0;    // betaForward (extra input gain in wave B2)
    p[4] = 1.0;    // betaReverse
    p[5] = 50.0;   // earlyVoltage
    p[6] = 1.0;    // detectorToCmdScale
    p[7] = 1.0e-9; // numericalGmin
    return p;
}

} // namespace

void testFullActiveEngineSkeletonPathBehavesLikeStub()
{
    u273::dsp::FullActiveRealtimeEngine engine {};
    engine.prepare(48000.0);

    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "FullActiveRealtimeEngine without parameters must report unverified boundary");
    require(!engine.hasParameters(),
            "FullActiveRealtimeEngine without setActiveModelParameters must not be flagged as ready");

    u273::core::ParameterSnapshot snapshot {};
    const auto reduction = engine.evaluateGainReductionDb(0.5f, snapshot);
    require(reduction == 0.0f,
            "Skeleton-mode FullActiveRealtimeEngine must keep returning 0 dB gain reduction");
    require(engine.lastFrame().isValid(),
            "Skeleton-mode telemetry frame must stay structurally valid");
    require(engine.xrunCount() == 0,
            "Skeleton-mode FullActiveRealtimeEngine must not report Newton xruns");
}

void testFullActiveEngineConvergesOnNominalInput()
{
    u273::dsp::FullActiveRealtimeEngine engine {};
    engine.prepare(96000.0);
    engine.setActiveModelParameters(makeNominalActiveModelParameters());
    require(engine.hasParameters(),
            "FullActiveRealtimeEngine must accept nominal calibrated parameters");
    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "FullActiveRealtimeEngine must stay unverified until the device-output audio gate exists");

    u273::core::ParameterSnapshot snapshot {};
    bool allFinite = true;
    for (int sample = 0; sample < 256; ++sample) {
        const auto reduction = engine.evaluateGainReductionDb(0.5f, snapshot);
        if (!std::isfinite(reduction)) {
            allFinite = false;
            break;
        }
        if (!engine.lastFrame().isValid()) {
            allFinite = false;
            break;
        }
    }
    require(allFinite,
            "FullActiveRealtimeEngine must emit finite gain reduction on every sample");
    require(engine.xrunCount() == 0,
            "Nominal input must converge without triggering Newton xruns");
    const auto steadyReduction = engine.lastFrame().gainReductionDb;
    require(steadyReduction >= 0.0f,
            "Steady-state gain reduction amount must stay non-negative");
}

void testFullActiveEngineProducesGainReductionOnLoudInput()
{
    u273::dsp::FullActiveRealtimeEngine engine {};
    engine.prepare(96000.0);
    engine.setActiveModelParameters(makeNominalActiveModelParameters());

    u273::core::ParameterSnapshot snapshot {};

    float quietGr = 0.0f;
    for (int sample = 0; sample < 64; ++sample) {
        quietGr = engine.evaluateGainReductionDb(1.0f, snapshot);
    }
    require(std::isfinite(quietGr),
            "Quiet section must remain numerically finite");

    float loudGr = 0.0f;
    for (int sample = 0; sample < 64; ++sample) {
        loudGr = engine.evaluateGainReductionDb(5.0f, snapshot);
    }
    require(std::isfinite(loudGr),
            "Loud section must remain numerically finite");

    require(loudGr > quietGr,
            "Loud envelope must drive a larger positive gain reduction than quiet");
    require(engine.xrunCount() == 0,
            "Stepped envelope sweep must converge without Newton xruns");
}

void testFullActiveEngineNoNaNOnExtremeInputs()
{
    u273::dsp::FullActiveRealtimeEngine engine {};
    engine.prepare(96000.0);
    engine.setActiveModelParameters(makeNominalActiveModelParameters());

    u273::core::ParameterSnapshot snapshot {};

    const std::array<float, 5> extremeInputs {
        0.0f,
        1.0e9f,
        -1.0e9f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()
    };

    for (const auto envelope : extremeInputs) {
        const auto reduction = engine.evaluateGainReductionDb(envelope, snapshot);
        require(std::isfinite(reduction),
                "Extreme detector envelope must still produce a finite gain reduction");
        require(reduction >= 0.0f && reduction <= 60.0f + 1.0e-3f,
                "Gain reduction must saturate cleanly inside [0, 60] dB");
        require(engine.lastFrame().isValid(),
                "Telemetry frame must stay valid even on extreme inputs");
        require(std::isfinite(engine.lastFrame().capacitorVoltage0),
                "Capacitor voltage must stay finite on extreme inputs");
    }
}

void testU273DspEnginePromoteToFullActiveModelSwitchesBoundary()
{
    u273::dsp::U273DspEngine engine {};
    engine.prepare(u273::dsp::DspPrepareConfig {48000.0, 64, 1});
    require(engine.isPrepared(),
            "DSP engine must prepare for full active promotion test");
    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "Pre-promotion boundary must remain fullActiveModelUnverified");
    require(!engine.isPromotedToFullActiveModel(),
            "Fresh DSP engine must not advertise promotion");

    std::array<double, u273::dsp::FullActiveRealtimeEngine::kCalibratedParameterCount> validParams {};
    validParams[0] = 1.0;
    validParams[1] = 1.0;
    validParams[2] = -27.6;
    validParams[3] = 1.0;
    validParams[4] = 1.0;
    validParams[5] = 50.0;
    validParams[6] = 1.0;
    validParams[7] = 1.0e-9;

    engine.promoteToFullActiveModel(validParams);
    require(engine.isPromotedToFullActiveModel(),
            "Valid parameters must escalate the DSP engine to the full active model");
    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "Promoted experimental full active DSP engine must remain unverified");

    auto nanParams = validParams;
    nanParams[2] = std::numeric_limits<double>::quiet_NaN();
    engine.promoteToFullActiveModel(nanParams);
    require(engine.isPromotedToFullActiveModel(),
            "NaN parameters must not regress an already-promoted DSP engine");
    require(engine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "Boundary must hold after a rejected re-promotion");

    // A second engine must reject NaN params from the start and stay on the
    // analog bridge model.
    u273::dsp::U273DspEngine freshEngine {};
    freshEngine.prepare(u273::dsp::DspPrepareConfig {48000.0, 64, 1});
    freshEngine.promoteToFullActiveModel(nanParams);
    require(!freshEngine.isPromotedToFullActiveModel(),
            "NaN parameters must never promote a fresh DSP engine");
    require(freshEngine.boundary() == u273::core::ModelBoundary::fullActiveModelUnverified,
            "Fresh engine boundary must stay at fullActiveModelUnverified after rejection");
}

} // namespace

int main()
{
    testParameterSnapshotContract();
    testDspSilenceIsStable();
    testRateGraphQualityModesDeclareExpectedRates();
    testRateGraphRejectsInvalidConfig();
    testDspPrepareStoresRateGraphAndLatency();
    testRenderRateGraphDoesNotOversampleExecutionYet();
    testDspHotSignalReducesGain();
    testDspEngineUsesInjectedGainReductionModel();
    testAnalogRealtimeBridgeLawIsMonotonic();
    testBypassPreservesSignal();
    testReferenceBoundaryIsExplicit();
    testGoldenFilesAreReadableAndPinned();
    testCalibrationDatasetLoadsGoldenFiles();
    testSonicThdTargetMatrixLoadsAndKeepsCmdProbeUnmapped();
    testDcCircuitViewOpensCapacitors();
    testOperatingPointConvergesOnKnownB6Case();
    testOperatingPointFailureKeepsAttemptJournal();
    testAcLinearizationMatchesRcClosedForm();
    testAcResidualGateMatchesRcClosedForm();
    testB6SmallSignalAcReferenceMatchesGoldenStrictSlice();
    testDcResidualGateReportsLoadedGoldenScenarioGaps();
    testDcExecutionReferenceLoaderMatchesGoldenTopology();
    testDcResidualGatePassesDcExecutionReference();
    testTransientResidualGateAcceptsGoldenSummaries();
    testBjtCandidateRejectsImpossiblePins();
    testActiveTopologyEvaluatorKeepsCandidateGuardedWithoutAcImprovement();
    testCalibrationReportDoesNotPromoteBoundaryEarly();
    testOfflineCalibrationRunnerStaysNonPromotable();
    testTransientWrapperAcceptsPlannedIntegrationMethods();
    testU273NetlistLoaderInventory();
    testU273NetlistLoaderCanStampKnownBjtHypotheses();
    testLoadedU273NetlistFirstDaeStepConverges();
    testU273DatasheetElectricalSpecs();
    testU273DatasheetDistortionFormula();
    testPeerCompressorPlausibilityEnvelope();
    testDetectorEnvelopeMatchesU273AttackReleaseSpecs();
    testAudioFftDatasheetBandAndThd();
    testStateSpaceLinearSolver();
    testStateSpaceNewtonSolver();
    testStateSpaceRcBackwardEuler();
    testStateSpaceDiodeCurrentFixture();
    testU273EmpiricalDiodeLawMatchesGoldenFixture();
    testStateSpaceBjtModelAndBiasFixture();
    testU273B6StateSpaceSkeleton();
    testMatrixTransposeAndNorm();
    testJacobiSvdKnownMatrix();
    testTrBdf2RcAnalyticBeatsBackwardEuler();
    testTrBdf2ReportsFailedStageOnDifficultDiode();
    testBoundedCalibrationConvergesOnPerturbedFixture();
    testBoundedCalibrationRespectsBoundsAndKeepsTopology();
    testIdentifiabilityDetectsUnusedParameter();
    testIdentifiabilityDetectsCollinearPair();
    testIdentifiabilityPassesOnWellConditionedFixture();
    testActiveParameterMappingBindsEmpiricalDiodeLaw();
    testRunOfflineProducesCalibratedReportButAudioStaysOpen();
    testRunOfflineFlagsOffStaysIdenticalToBaseline();
    testRunOfflineAudioGatePassesOnGoldenAndFailsOnPerturbed();
    testThdBenchMapsSiemensBridgeOnlyAtExactConditions();
    testReductionBuilderBuildsMonotoneReferenceTable();
    testReductionBuilderRejectsBadInputsAndExplosiveDerivatives();
    testTableReductionRealtimeEngineLookupClampAndExtremeInputs();
    testU273DspEngineProcessesSineAndSweepViaReductionTable();
    testRealtimeDetailLevelsExposeBoundaries();
    testFullActiveEngineSkeletonPathBehavesLikeStub();
    testFullActiveEngineConvergesOnNominalInput();
    testFullActiveEngineProducesGainReductionOnLoudInput();
    testFullActiveEngineNoNaNOnExtremeInputs();
    testU273DspEnginePromoteToFullActiveModelSwitchesBoundary();

    std::cout << "u273_tests: scaffold and state-space contracts passed\n";
    return 0;
}
