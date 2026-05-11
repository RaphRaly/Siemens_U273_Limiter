#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#include "u273/core/U273Core.h"
#include "u273/dsp/AnalogRealtimeEngine.h"
#include "u273/dsp/U273DspEngine.h"
#include "u273/dsp/DetectorEnvelope.h"
#include "u273/dsp/RealtimeGainReductionModel.h"
#include "u273/reference/GoldenValidationSuite.h"
#include "u273/reference/ReferenceValidator.h"
#include "u273/reference/ScientificReferenceModel.h"
#include "u273/reference/U273TechnicalSpecs.h"
#include "u273/reference/calibration/ActiveTopologyCandidate.h"
#include "u273/reference/calibration/ActiveTopologyEvaluator.h"
#include "u273/reference/calibration/B6B11CalibrationRunner.h"
#include "u273/reference/calibration/CalibrationDataset.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/calibration/LinearizedAcSolver.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/calibration/TransientReferenceSolver.h"
#include "u273/reference/state_space/ComponentModels.h"
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
    require(!report.gates.ac, "offline runner AC gate must remain open until the model matches golden AC");
    require(!report.gates.referenceAc, "offline runner reference AC gate must report the current mismatch");
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

} // namespace

int main()
{
    testParameterSnapshotContract();
    testDspSilenceIsStable();
    testDspHotSignalReducesGain();
    testDspEngineUsesInjectedGainReductionModel();
    testAnalogRealtimeBridgeLawIsMonotonic();
    testBypassPreservesSignal();
    testReferenceBoundaryIsExplicit();
    testGoldenFilesAreReadableAndPinned();
    testCalibrationDatasetLoadsGoldenFiles();
    testDcCircuitViewOpensCapacitors();
    testOperatingPointConvergesOnKnownB6Case();
    testOperatingPointFailureKeepsAttemptJournal();
    testAcLinearizationMatchesRcClosedForm();
    testAcResidualGateMatchesRcClosedForm();
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

    std::cout << "u273_tests: scaffold and state-space contracts passed\n";
    return 0;
}
