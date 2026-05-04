#pragma once

#include <cstddef>
#include <cstdint>

namespace u273::core {

inline constexpr std::uint32_t kParameterSnapshotVersion = 1U;
inline constexpr int kMaxRealtimeChannels = 2;
inline constexpr double kDefaultSampleRate = 48000.0;
inline constexpr float kMinMeterDb = -120.0f;

} // namespace u273::core
