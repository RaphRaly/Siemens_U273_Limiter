#pragma once

#include <array>
#include <cstdint>

namespace u273::dsp {

enum class RealtimeQualityMode : std::uint8_t {
    eco = 0,
    precise,
    render
};

enum class MultiratePhaseMode : std::uint8_t {
    linearPhaseIntegerLatency = 0
};

struct RateGraphConfig {
    double hostSampleRate {48000.0};
    int maxBlockSize {};
    RealtimeQualityMode mode {RealtimeQualityMode::precise};
};

struct RateStage {
    const char* name {};
    int oversamplingFactor {1};
    int latencySamples {};
    double latencySamplesExact {};
    int hostReportedLatencySamples {};
    int dryCompensationSamples {};
    bool enabled {true};
    double processingSampleRate {};
    double targetBandwidthHz {};
    bool latencyCompensated {true};
    bool executesOversampling {false};
    MultiratePhaseMode phaseMode {MultiratePhaseMode::linearPhaseIntegerLatency};
};

struct RateGraph {
    static constexpr int kMaxStages = 7;

    RateGraphConfig config {};
    std::array<RateStage, kMaxStages> stages {};
    int stageCount {};
    bool valid {false};
    bool oversamplingExecutionEnabled {false};
    double latencySamplesExact {};
    int hostReportedLatencySamples {};
    int dryCompensationSamples {};
    bool deltaPathEnabled {true};
    bool dryPathTransparentAtZeroReduction {true};

    [[nodiscard]] bool isValid() const noexcept { return valid; }
};

[[nodiscard]] bool isRealtimeQualityModeValid(RealtimeQualityMode mode) noexcept;
[[nodiscard]] RateGraph buildRateGraph(const RateGraphConfig& config) noexcept;
[[nodiscard]] bool setRateStageOversamplingExecution(RateGraph& graph,
                                                     const char* name,
                                                     bool executesOversampling) noexcept;
[[nodiscard]] bool setRateStageLatency(RateGraph& graph,
                                       const char* name,
                                       double latencySamplesExact,
                                       int hostReportedLatencySamples,
                                       int dryCompensationSamples) noexcept;
[[nodiscard]] int totalLatencySamples(const RateGraph& graph) noexcept;
[[nodiscard]] const RateStage* findRateStage(const RateGraph& graph, const char* name) noexcept;

} // namespace u273::dsp
