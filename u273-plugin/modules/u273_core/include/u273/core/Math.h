#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "u273/core/Constants.h"

namespace u273::core {

inline bool isFinite(float value) noexcept
{
    return std::isfinite(value);
}

inline bool isFinite(double value) noexcept
{
    return std::isfinite(value);
}

inline float clampFloat(float value, float lower, float upper) noexcept
{
    return std::min(std::max(value, lower), upper);
}

inline float dbToLinear(float db) noexcept
{
    if (!isFinite(db)) {
        return 1.0f;
    }

    if (db <= kMinMeterDb) {
        return 0.0f;
    }

    return std::pow(10.0f, db / 20.0f);
}

inline float linearToDb(float linear, float floorDb = kMinMeterDb) noexcept
{
    if (!isFinite(linear) || linear <= 0.0f) {
        return floorDb;
    }

    return std::max(floorDb, 20.0f * std::log10(linear));
}

inline float absMax(float lhs, float rhs) noexcept
{
    return std::max(std::fabs(lhs), std::fabs(rhs));
}

} // namespace u273::core
