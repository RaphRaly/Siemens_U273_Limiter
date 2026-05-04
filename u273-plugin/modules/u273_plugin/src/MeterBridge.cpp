#include "u273/plugin/MeterBridge.h"

namespace u273::plugin {

void MeterBridge::publish(const u273::core::MeterFrame& frame) noexcept
{
    inputPeakDb_.store(frame.inputPeakDb, std::memory_order_relaxed);
    outputPeakDb_.store(frame.outputPeakDb, std::memory_order_relaxed);
    gainReductionDb_.store(frame.gainReductionDb, std::memory_order_relaxed);
    clipFlag_.store(frame.clipFlag, std::memory_order_relaxed);
    sequence_.store(frame.sequence, std::memory_order_release);
}

u273::core::MeterFrame MeterBridge::readLatest() const noexcept
{
    u273::core::MeterFrame frame {};
    frame.sequence = sequence_.load(std::memory_order_acquire);
    frame.inputPeakDb = inputPeakDb_.load(std::memory_order_relaxed);
    frame.outputPeakDb = outputPeakDb_.load(std::memory_order_relaxed);
    frame.gainReductionDb = gainReductionDb_.load(std::memory_order_relaxed);
    frame.clipFlag = clipFlag_.load(std::memory_order_relaxed);
    return frame;
}

} // namespace u273::plugin
