#pragma once

#include <string_view>

namespace u273::core::param_id {

inline constexpr std::string_view inputGainDb = "inputGainDb";
inline constexpr std::string_view outputGainDb = "outputGainDb";
inline constexpr std::string_view mode = "mode";
inline constexpr std::string_view drive = "drive";
inline constexpr std::string_view detectorScale = "detectorScale";
inline constexpr std::string_view attackMs = "attackMs";
inline constexpr std::string_view releaseMs = "releaseMs";
inline constexpr std::string_view mix = "mix";
inline constexpr std::string_view calibrationLevelDb = "calibrationLevelDb";
inline constexpr std::string_view bypass = "bypass";

} // namespace u273::core::param_id
