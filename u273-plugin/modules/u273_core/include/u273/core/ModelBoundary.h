#pragma once

#include <cstdint>

namespace u273::core {

// Explicit scientific confidence state. This prevents provisional circuit
// models from being presented as validated realtime closure.
enum class ModelBoundary : std::uint8_t {
    unknown = 0,
    passWithGuardedBoundaries,
    guardedRealtimeSurrogate,
    fullActiveModelUnverified
};

inline constexpr const char* toString(ModelBoundary boundary) noexcept
{
    switch (boundary) {
        case ModelBoundary::passWithGuardedBoundaries:
            return "PASS_WITH_GUARDED_BOUNDARIES";
        case ModelBoundary::guardedRealtimeSurrogate:
            return "GUARDED_REALTIME_SURROGATE";
        case ModelBoundary::fullActiveModelUnverified:
            return "FULL_ACTIVE_MODEL_UNVERIFIED";
        case ModelBoundary::unknown:
        default:
            return "UNKNOWN";
    }
}

inline constexpr ModelBoundary currentScientificBoundary() noexcept
{
    return ModelBoundary::passWithGuardedBoundaries;
}

} // namespace u273::core
