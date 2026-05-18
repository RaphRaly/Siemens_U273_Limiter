#pragma once

#include <array>
#include <cstdint>

namespace u273::dsp {

enum class RealtimeQualityMode : std::uint8_t {
    eco = 0,
    precise,
    render
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
    bool enabled {true};
    double processingSampleRate {};
    double targetBandwidthHz {};
    bool latencyCompensated {true};
};

struct RateGraph {
    static constexpr int kMaxStages = 7;

    RateGraphConfig config {};
    std::array<RateStage, kMaxStages> stages {};
    int stageCount {};
    bool valid {false};
    bool oversamplingExecutionEnabled {false};
    bool deltaPathEnabled {true};
    bool dryPathTransparentAtZeroReduction {true};

    [[nodiscard]] bool isValid() const noexcept { return valid; }
};

[[nodiscard]] bool isRealtimeQualityModeValid(RealtimeQualityMode mode) noexcept;
[[nodiscard]] RateGraph buildRateGraph(const RateGraphConfig& config) noexcept;
[[nodiscard]] int totalLatencySamples(const RateGraph& graph) noexcept;
[[nodiscard]] const RateStage* findRateStage(const RateGraph& graph, const char* name) noexcept;

} // namespace u273::dsp
