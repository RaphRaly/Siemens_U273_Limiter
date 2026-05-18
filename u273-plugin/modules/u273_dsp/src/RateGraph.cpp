#include "u273/dsp/RateGraph.h"

#include <cstring>

#include "u273/core/Math.h"

namespace u273::dsp {

namespace {

constexpr const char* kAudioInputStage = "audioInput";
constexpr const char* kDryPathStage = "dryPath";
constexpr const char* kSidechainStage = "sidechain";
constexpr const char* kGainCellStage = "gainCell";
constexpr const char* kTruePeakStage = "truePeak";
constexpr const char* kUiMeterStage = "uiMeter";
constexpr const char* kAudioOutputStage = "audioOutput";
constexpr double kUiMeterRateHz = 60.0;

struct MultiRateFactors {
    int sidechain {1};
    int gainCell {1};
    int truePeak {4};
};

[[nodiscard]] double sidechainBandwidthTargetHz(RealtimeQualityMode mode) noexcept
{
    switch (mode) {
        case RealtimeQualityMode::eco:
            return 100000.0;
        case RealtimeQualityMode::precise:
            return 200000.0;
        case RealtimeQualityMode::render:
            return 400000.0;
        default:
            return 0.0;
    }
}

[[nodiscard]] MultiRateFactors selectMultiRateFactors(double hostSampleRate,
                                                      RealtimeQualityMode mode) noexcept
{
    if (hostSampleRate <= 50000.0) {
        switch (mode) {
            case RealtimeQualityMode::eco:
                return {4, 2, 4};
            case RealtimeQualityMode::precise:
                return {8, 4, 8};
            case RealtimeQualityMode::render:
                return {16, 8, 16};
            default:
                return {};
        }
    }

    if (hostSampleRate <= 100000.0) {
        switch (mode) {
            case RealtimeQualityMode::eco:
                return {2, 1, 4};
            case RealtimeQualityMode::precise:
                return {4, 2, 8};
            case RealtimeQualityMode::render:
                return {8, 4, 16};
            default:
                return {};
        }
    }

    switch (mode) {
        case RealtimeQualityMode::eco:
            return {1, 1, 4};
        case RealtimeQualityMode::precise:
            return {2, 1, 8};
        case RealtimeQualityMode::render:
            return {4, 2, 16};
        default:
            return {};
    }
}

[[nodiscard]] RateStage makeHostStage(const char* name, double hostSampleRate) noexcept
{
    return RateStage {name, 1, 0, true, hostSampleRate, hostSampleRate * 0.5, true};
}

[[nodiscard]] RateStage makeOversampledStage(const char* name,
                                             int factor,
                                             double hostSampleRate,
                                             double targetBandwidthHz) noexcept
{
    return RateStage {
        name,
        factor,
        0,
        true,
        hostSampleRate * static_cast<double>(factor),
        targetBandwidthHz,
        true};
}

[[nodiscard]] bool isConfigValid(const RateGraphConfig& config) noexcept
{
    return u273::core::isFinite(config.hostSampleRate)
        && config.hostSampleRate > 0.0
        && config.maxBlockSize >= 0
        && isRealtimeQualityModeValid(config.mode);
}

void appendStage(RateGraph& graph, const RateStage& stage) noexcept
{
    if (graph.stageCount >= RateGraph::kMaxStages) {
        graph.valid = false;
        return;
    }

    graph.stages[static_cast<std::size_t>(graph.stageCount)] = stage;
    ++graph.stageCount;
}

} // namespace

bool isRealtimeQualityModeValid(RealtimeQualityMode mode) noexcept
{
    switch (mode) {
        case RealtimeQualityMode::eco:
        case RealtimeQualityMode::precise:
        case RealtimeQualityMode::render:
            return true;
        default:
            return false;
    }
}

RateGraph buildRateGraph(const RateGraphConfig& config) noexcept
{
    RateGraph graph {};
    graph.config = config;

    if (!isConfigValid(config)) {
        return graph;
    }

    const auto factors = selectMultiRateFactors(config.hostSampleRate, config.mode);
    if (factors.sidechain <= 0 || factors.gainCell <= 0 || factors.truePeak <= 0) {
        return graph;
    }

    graph.valid = true;
    graph.oversamplingExecutionEnabled = false;
    graph.deltaPathEnabled = true;
    graph.dryPathTransparentAtZeroReduction = true;

    const auto sidechainTarget = sidechainBandwidthTargetHz(config.mode);
    appendStage(graph, makeHostStage(kAudioInputStage, config.hostSampleRate));
    appendStage(graph, makeHostStage(kDryPathStage, config.hostSampleRate));
    appendStage(graph, makeOversampledStage(
        kSidechainStage, factors.sidechain, config.hostSampleRate, sidechainTarget));
    appendStage(graph, makeOversampledStage(
        kGainCellStage, factors.gainCell, config.hostSampleRate, 0.0));
    appendStage(graph, makeOversampledStage(
        kTruePeakStage, factors.truePeak, config.hostSampleRate, 0.0));
    appendStage(graph, RateStage {kUiMeterStage, 1, 0, true, kUiMeterRateHz, 0.0, true});
    appendStage(graph, makeHostStage(kAudioOutputStage, config.hostSampleRate));

    return graph;
}

int totalLatencySamples(const RateGraph& graph) noexcept
{
    if (!graph.isValid()) {
        return 0;
    }

    auto latency = 0;
    for (auto index = 0; index < graph.stageCount; ++index) {
        const auto& stage = graph.stages[static_cast<std::size_t>(index)];
        if (stage.enabled) {
            latency += stage.latencySamples;
        }
    }

    return latency;
}

const RateStage* findRateStage(const RateGraph& graph, const char* name) noexcept
{
    if (name == nullptr || !graph.isValid()) {
        return nullptr;
    }

    for (auto index = 0; index < graph.stageCount; ++index) {
        const auto& stage = graph.stages[static_cast<std::size_t>(index)];
        if (stage.name != nullptr && std::strcmp(stage.name, name) == 0) {
            return &stage;
        }
    }

    return nullptr;
}

} // namespace u273::dsp
