#pragma once

#include <cmath>

namespace u273::reference::calibration {

struct BoundedParameter {
    double value {};
    double lower {};
    double upper {};

    [[nodiscard]] bool isValid() const noexcept
    {
        return std::isfinite(value)
            && std::isfinite(lower)
            && std::isfinite(upper)
            && lower <= upper
            && value >= lower
            && value <= upper;
    }

    [[nodiscard]] bool touchesBound(double relativeTolerance = 1.0e-6) const noexcept
    {
        if (!isValid()) {
            return true;
        }
        const auto span = upper - lower;
        const auto tolerance = span > 0.0 ? span * relativeTolerance : relativeTolerance;
        return std::fabs(value - lower) <= tolerance || std::fabs(value - upper) <= tolerance;
    }
};

struct ActiveModelParameters {
    BoundedParameter diodeA {308.0, 250.0, 380.0};
    BoundedParameter diodeB {0.16, 0.08, 0.28};
    BoundedParameter logIs {-27.631021115928547, -34.538776394910684, -18.420680743952367};
    BoundedParameter betaForward {120.0, 20.0, 300.0};
    BoundedParameter betaReverse {2.0, 0.1, 20.0};
    BoundedParameter earlyVoltage {80.0, 20.0, 250.0};
    BoundedParameter detectorToCmdScale {1.0, 0.05, 4.0};
    BoundedParameter numericalGmin {1.0e-12, 1.0e-15, 1.0e-8};

    [[nodiscard]] bool isValid() const noexcept
    {
        return diodeA.isValid()
            && diodeB.isValid()
            && logIs.isValid()
            && betaForward.isValid()
            && betaReverse.isValid()
            && earlyVoltage.isValid()
            && detectorToCmdScale.isValid()
            && numericalGmin.isValid();
    }

    [[nodiscard]] bool hasParameterOnBound() const noexcept
    {
        return diodeA.touchesBound()
            || diodeB.touchesBound()
            || logIs.touchesBound()
            || betaForward.touchesBound()
            || betaReverse.touchesBound()
            || earlyVoltage.touchesBound()
            || detectorToCmdScale.touchesBound()
            || numericalGmin.touchesBound();
    }
};

} // namespace u273::reference::calibration
