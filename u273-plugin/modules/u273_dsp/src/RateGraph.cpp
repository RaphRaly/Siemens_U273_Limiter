#include "u273/dsp/RateGraph.h"

#include <cstring>

#include "u273/core/Math.h"

namespace u273::dsp {

namespace {

constexpr const char* kAudioInputStage = "audioInput";
constexpr const char* kSidechainStage = "sidechain";
constexpr const char* kGainCellStage = "gainCell";
constexpr const char* kAudioOutputStage = "audioOutput";

[[nodiscard]] int controlOversamplingFactor(RealtimeQualityMode mode) noexcept
{
    switch (mode) {
        case RealtimeQualityMode::eco:
            return 1;
        case RealtimeQualityMode::precise:
            return 2;
        case RealtimeQualityMode::render:
            return 4;
        default:
            return 0;
    }
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

    const auto controlFactor = controlOversamplingFactor(config.mode);
    if (controlFactor <= 0) {
        return graph;
    }

    graph.valid = true;
    graph.oversamplingExecutionEnabled = false;

    appendStage(graph, RateStage {kAudioInputStage, 1, 0, true});
    appendStage(graph, RateStage {kSidechainStage, controlFactor, 0, true});
    appendStage(graph, RateStage {kGainCellStage, controlFactor, 0, true});
    appendStage(graph, RateStage {kAudioOutputStage, 1, 0, true});

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
