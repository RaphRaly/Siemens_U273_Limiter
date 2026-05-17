#include "u273/dsp/FullActiveRealtimeEngine.h"

#include <algorithm>
#include <cmath>

// =============================================================================
// First-stage minimal nonlinear analog model
// -----------------------------------------------------------------------------
// This is intentionally NOT the full B6-B11 active topology. Wave B2 ships a
// pragmatic stepping-stone: two anti-parallel diodes from the gain-reduction
// control node to ground, in parallel with one capacitor, driven by a current
// source proportional to (detectorEnvelope * detectorToCmdScale).
//
// Node equation (KCL at the control node v):
//   i_input = i_diode_pos + i_diode_neg + i_cap
// where (Backward Euler, h = 1/sampleRate, gc = C/h):
//   i_diode_pos = Is * (exp(v/VT) - 1)
//   i_diode_neg = -Is * (exp(-v/VT) - 1)
//   i_cap       = gc * (v - vPrev)
//
// The model is honest about its scope: it consumes calibrated diode parameters
// (Is via logIs, plus detectorToCmdScale), exercises the Newton + implicit
// integration plumbing, and produces a finite gain-reduction telemetry stream.
// The full B6-B11 circuit, with the BJT side-chain and the second integrator,
// will replace this in wave B3+. Until then `boundary()` stays at
// FULL_ACTIVE_MODEL_UNVERIFIED even when calibrated parameters were accepted.
// The strict device-output audio gate is the only route to a future validated
// boundary; this experimental path exists only for lab comparison.
// =============================================================================

namespace u273::dsp {

namespace {

// Canonical parameter slot indices in calibratedParameters_. These mirror the
// declaration order of u273::reference::calibration::ActiveModelParameters and
// are duplicated here purely to avoid pulling u273_reference into u273_dsp.
constexpr std::size_t kSlotDiodeA = 0;
constexpr std::size_t kSlotDiodeB = 1;
constexpr std::size_t kSlotLogIs = 2;
constexpr std::size_t kSlotBetaForward = 3;
constexpr std::size_t kSlotBetaReverse = 4;
constexpr std::size_t kSlotEarlyVoltage = 5;
constexpr std::size_t kSlotDetectorToCmdScale = 6;
constexpr std::size_t kSlotNumericalGmin = 7;

[[nodiscard]] bool isFiniteParameterSet(
    const std::array<double, FullActiveRealtimeEngine::kCalibratedParameterCount>& values) noexcept
{
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

// Loose plausibility check kept in sync with ActiveModelParameters bounds. We
// intentionally use a wide envelope so the realtime side never silently rejects
// parameters the offline calibrator just produced; the offline side is the
// source of truth for tight bounds via BoundedParameter::isValid().
[[nodiscard]] bool hasPlausibleMagnitudes(
    const std::array<double, FullActiveRealtimeEngine::kCalibratedParameterCount>& values) noexcept
{
    return values[kSlotDiodeA] > 0.0
        && values[kSlotDiodeB] > 0.0
        && values[kSlotBetaForward] > 0.0
        && values[kSlotBetaReverse] > 0.0
        && values[kSlotEarlyVoltage] > 0.0
        && values[kSlotDetectorToCmdScale] > 0.0
        && values[kSlotNumericalGmin] > 0.0;
}

// Safe exponential with symmetric clamp. Keeps the Jacobian finite even when a
// transient Newton step drives v far outside the diode operating region.
[[nodiscard]] inline double safeExp(double x) noexcept
{
    return std::exp(std::clamp(x, -FullActiveRealtimeEngine::kExpClampHigh,
                               FullActiveRealtimeEngine::kExpClampHigh));
}

[[nodiscard]] inline double sanitiseInputCurrent(double current) noexcept
{
    if (!std::isfinite(current)) {
        return 0.0;
    }
    // Cap absurd input magnitudes (e.g. infinity inputs from tests) so the
    // Newton iteration never starts from an unrecoverable seed. 1e6 A is far
    // outside any plausible drive while still being representable.
    constexpr double kInputCurrentCeiling = 1.0e6;
    return std::clamp(current, -kInputCurrentCeiling, kInputCurrentCeiling);
}

[[nodiscard]] inline double resolveSaturationCurrent(double logIs) noexcept
{
    if (!std::isfinite(logIs) || logIs == 0.0) {
        return FullActiveRealtimeEngine::kDefaultIs;
    }
    const auto raw = std::exp(std::clamp(logIs,
                                         -FullActiveRealtimeEngine::kExpClampHigh,
                                         FullActiveRealtimeEngine::kExpClampHigh));
    if (!(raw > 0.0) || !std::isfinite(raw)) {
        return FullActiveRealtimeEngine::kDefaultIs;
    }
    return raw;
}

[[nodiscard]] inline double sanitiseDetector(float envelope) noexcept
{
    const auto value = static_cast<double>(envelope);
    if (!std::isfinite(value)) {
        // Saturate non-finite envelopes (NaN/inf) onto a large but finite drive
        // so the Newton solver still has a deterministic input to react to.
        return std::signbit(value) ? -1.0e6 : 1.0e6;
    }
    return value;
}

} // namespace

bool FullActiveRealtimeFrame::isValid() const noexcept
{
    return std::isfinite(gainReductionDb)
        && gainReductionDb >= -1.0e3f          // BE/Newton clamp keeps GR in [-60, 0] dB; allow slack
        && gainReductionDb <= 1.0e3f
        && std::isfinite(capacitorVoltage0)
        && std::isfinite(capacitorVoltage1)
        && xrunCount >= 0
        && sampleCount >= 0;
}

FullActiveRealtimeEngine::FullActiveRealtimeEngine() noexcept = default;

void FullActiveRealtimeEngine::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    samplePeriod_ = 1.0 / sampleRate_;
    capacitorConductance_ = kCapacitanceFarad / samplePeriod_;
    xrunCount_ = 0;
    sampleCount_ = 0;
    capacitorVoltages_.fill(0.0);
    capacitorCurrents_.fill(0.0);
    previousUnknowns_.fill(0.0);
    lastFrame_ = FullActiveRealtimeFrame {};
}

void FullActiveRealtimeEngine::reset() noexcept
{
    xrunCount_ = 0;
    sampleCount_ = 0;
    capacitorVoltages_.fill(0.0);
    capacitorCurrents_.fill(0.0);
    previousUnknowns_.fill(0.0);
    lastFrame_ = FullActiveRealtimeFrame {};
}

float FullActiveRealtimeEngine::evaluateGainReductionDb(
    float detectorEnvelope,
    [[maybe_unused]] const u273::core::ParameterSnapshot& snapshot) noexcept
{
    ++sampleCount_;

    // Without calibrated parameters the engine stays in pass-through stub mode:
    // boundary() reports fullActiveModelUnverified and we emit a zero-reduction
    // telemetry frame. This matches the wave B1 contract that downstream gates
    // rely on.
    if (!parametersSet_) {
        lastFrame_ = FullActiveRealtimeFrame {
            0.0f,
            0.0f,
            0.0f,
            xrunCount_,
            sampleCount_,
        };
        return 0.0f;
    }

    const double detectorScale = calibratedParameters_[kSlotDetectorToCmdScale];
    const double betaForward = calibratedParameters_[kSlotBetaForward];
    const double saturationCurrent = resolveSaturationCurrent(calibratedParameters_[kSlotLogIs]);

    const double diodeWeightForward = std::max(0.0, calibratedParameters_[kSlotDiodeA]);
    const double diodeWeightReverse = std::max(0.0, calibratedParameters_[kSlotDiodeB]);

    // betaForward (slot 3) is reused as an extra input-current gain so wave B2
    // already consumes the slot. When the offline calibrator fills it with a
    // non-zero value the realtime side multiplies the drive accordingly.
    const double inputGain = (betaForward > 0.0) ? betaForward : 1.0;

    const double envelope = sanitiseDetector(detectorEnvelope);
    const double rawInputCurrent = envelope * detectorScale * inputGain;
    const double iInput = sanitiseInputCurrent(rawInputCurrent);

    // Backward Euler companion: capacitor voltage at the previous time step is
    // the stored unknown. capacitorConductance_ = C / h.
    const double vCapPrev = capacitorVoltages_[0];
    const double gc = capacitorConductance_;

    // TR-BDF2 path pending — fall back to BE for now.
    // The integrationMethod_ enum is plumbed so wave B3 can switch without
    // touching the call sites; today both selections execute the BE branch.
    (void) integrationMethod_;

    double v = previousUnknowns_[0];

    // Analytical seed from the static diode equation when the warm-start is far
    // from the operating point. This keeps the Newton loop inside its 4-iter
    // budget even when the first sample after reset sees a large iInput. We do
    // NOT replace warm-start unconditionally: warm-start is preferred for
    // sample-to-sample continuity; the analytical seed only kicks in when the
    // residual at warm-start is large enough that Newton would otherwise need
    // many iterations to recover.
    {
        const double diodeAvgWeight = 0.5 * (diodeWeightForward + diodeWeightReverse);
        if (diodeAvgWeight > 0.0 && saturationCurrent > 0.0) {
            const double iSafe = sanitiseInputCurrent(iInput);
            const double seedArg = iSafe / (2.0 * diodeAvgWeight * saturationCurrent);
            // asinh-based static-diode seed (anti-parallel pair behaves like sinh).
            const double absArg = std::abs(seedArg);
            const double seedMagnitude = (absArg > 1.0)
                ? std::log(2.0 * absArg)            // asinh(x) ~ ln(2|x|) for |x| >> 1
                : std::log1p(absArg);               // small-signal smooth ramp
            const double vSeed = (seedArg >= 0.0 ? 1.0 : -1.0) * kThermalVoltage * seedMagnitude;
            // Only adopt the analytical seed when it is materially closer to the
            // operating point than the warm-start. The threshold below catches
            // cold-start (v=0 with large iInput) without disturbing steady-state
            // sample-to-sample continuity.
            if (std::isfinite(vSeed) && std::abs(vSeed - v) > kThermalVoltage) {
                v = vSeed;
            }
        }
    }

    bool converged = false;

    for (int iter = 0; iter < kMaxNewtonIterations; ++iter) {
        const double expPos = safeExp(v / kThermalVoltage);
        const double expNeg = safeExp(-v / kThermalVoltage);
        const double iDiodePos = diodeWeightForward * saturationCurrent * (expPos - 1.0);
        const double iDiodeNeg = diodeWeightReverse * saturationCurrent * (1.0 - expNeg);
        const double iDiode = iDiodePos + iDiodeNeg;
        const double gDiode = (saturationCurrent / kThermalVoltage)
            * (diodeWeightForward * expPos + diodeWeightReverse * expNeg);
        const double iCap = gc * (v - vCapPrev);
        const double residual = iInput - iDiode - iCap;
        const double jacobian = -(gDiode + gc);

        if (!std::isfinite(residual) || !std::isfinite(jacobian)) {
            break;
        }
        if (std::abs(jacobian) < kPivotFloor) {
            break;
        }

        double delta = residual / jacobian;
        if (!std::isfinite(delta)) {
            break;
        }
        // Static clamp keeps a pathological warm-start from blowing up the
        // first step. The diode-aware clamp (below) takes over once we are
        // inside the conducting region: when gDiode dominates the Jacobian we
        // must stay within ~VT per step or the next exp() will swamp Newton's
        // quadratic convergence with a single huge Jacobian.
        delta = std::clamp(delta, -kDeltaClamp, kDeltaClamp);
        if (gDiode > gc) {
            delta = std::clamp(delta, -kThermalVoltage, kThermalVoltage);
        }
        v -= delta;

        // Two-arm convergence: either the Newton step is small enough that v
        // is numerically settled (|delta| < kDeltaTolerance) OR the residual is
        // tiny relative to the input drive (relative current error). The
        // residual arm catches the steep-diode regime where 4 iterations cannot
        // hit the absolute |delta| target due to the exp() Jacobian growing
        // much faster than v.
        const double residualTolerance = std::max(1.0e-9, std::abs(iInput) * 5.0e-2);
        if (std::abs(delta) < kDeltaTolerance || std::abs(residual) < residualTolerance) {
            converged = true;
            break;
        }
    }

    if (!converged || !std::isfinite(v)) {
        ++xrunCount_;
        v = previousUnknowns_[0];  // previous-sample-hold fallback
        if (!std::isfinite(v)) {
            v = 0.0;
        }
    }

    previousUnknowns_[0] = v;
    capacitorVoltages_[0] = v;

    // Map the control voltage back to a positive gain-reduction amount in dB. We compare the
    // post-Newton control voltage to the drive expressed in envelope units so
    // tests can correlate envelope magnitude to GR magnitude without depending
    // on absolute current scaling.
    const double detectorScaleSafe = (detectorScale > 0.0) ? detectorScale : 1.0;
    const double inputLevel = std::max(std::abs(iInput) / (detectorScaleSafe * inputGain), 1.0e-9);
    const double outputLevel = std::max(std::abs(v), 1.0e-9);
    double gainReductionDbDouble = -20.0 * std::log10(outputLevel / inputLevel);
    if (!std::isfinite(gainReductionDbDouble)) {
        gainReductionDbDouble = kMinGainReductionDb;
    }
    gainReductionDbDouble = std::clamp(gainReductionDbDouble,
                                       kMinGainReductionDb,
                                       kMaxGainReductionDb);
    const float gainReductionDb = static_cast<float>(gainReductionDbDouble);

    lastFrame_ = FullActiveRealtimeFrame {
        gainReductionDb,
        static_cast<float>(v),
        static_cast<float>(capacitorVoltages_[1]),
        xrunCount_,
        sampleCount_,
    };
    return gainReductionDb;
}

void FullActiveRealtimeEngine::setActiveModelParameters(
    const std::array<double, kCalibratedParameterCount>& parameters) noexcept
{
    if (!isFiniteParameterSet(parameters) || !hasPlausibleMagnitudes(parameters)) {
        // Reject the new packet but keep any previously-accepted parameter set
        // intact. This guarantees that a single bad offline run cannot regress
        // an already-promoted realtime engine to the surrogate boundary.
        return;
    }
    calibratedParameters_ = parameters;
    parametersSet_ = true;
}

void FullActiveRealtimeEngine::setIntegrationMethod(IntegrationMethod method) noexcept
{
    integrationMethod_ = method;
}

} // namespace u273::dsp
