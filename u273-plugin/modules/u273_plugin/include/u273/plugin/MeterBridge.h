#pragma once

#include <atomic>

#include "u273/core/MeterFrame.h"

namespace u273::plugin {

// Atomic handoff between audio thread and editor. Each field is stored
// independently; sequence is the release/acquire freshness marker.
class MeterBridge {
public:
    void publish(const u273::core::MeterFrame& frame) noexcept;
    [[nodiscard]] u273::core::MeterFrame readLatest() const noexcept;

private:
    std::atomic<float> inputPeakDb_ {u273::core::kMinMeterDb};
    std::atomic<float> outputPeakDb_ {u273::core::kMinMeterDb};
    std::atomic<float> gainReductionDb_ {0.0f};
    std::atomic<bool> clipFlag_ {false};
    std::atomic<std::uint64_t> sequence_ {0};
};

} // namespace u273::plugin
