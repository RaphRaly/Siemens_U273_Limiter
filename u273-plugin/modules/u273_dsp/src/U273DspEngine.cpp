#include "u273/dsp/U273DspEngine.h"

#include <algorithm>
#include <cmath>

#include "u273/core/Math.h"

namespace u273::dsp {

bool DspPrepareConfig::isValid() const noexcept
{
    return u273::core::isFinite(sampleRate)
        && sampleRate > 0.0
        && maxBlockSize >= 0
        && numChannels > 0
        && numChannels <= u273::core::kMaxRealtimeChannels
        && isRealtimeQualityModeValid(qualityMode);
}

U273DspEngine::U273DspEngine() noexcept
    : gainReductionModel_(&defaultGainReductionModel_)
{
}

U273DspEngine::U273DspEngine(RealtimeGainReductionModel& gainReductionModel) noexcept
    : gainReductionModel_(&gainReductionModel)
{
}

void U273DspEngine::prepare(const DspPrepareConfig& config) noexcept
{
    prepared_ = false;
    rateGraph_ = RateGraph {};
    sidechainOversamplingFactor_ = 1;
    gainCellOversamplingFactor_ = 1;

    if (!config.isValid()) {
        return;
    }

    config_ = config;
    rateGraph_ = buildRateGraph(
        RateGraphConfig {config.sampleRate, config.maxBlockSize, config.qualityMode});
    if (!rateGraph_.isValid()) {
        return;
    }

    if (const auto* sidechainStage = findRateStage(rateGraph_, "sidechain")) {
        sidechainOversamplingFactor_ = std::max(1, sidechainStage->oversamplingFactor);
        (void) setRateStageOversamplingExecution(rateGraph_, "sidechain", true);
    }

    if (const auto* gainCellStage = findRateStage(rateGraph_, "gainCell")) {
        gainCellOversamplingFactor_ = std::max(1, gainCellStage->oversamplingFactor);
    }

    if (!gainCellDeltaPath_.prepare(gainCellOversamplingFactor_, config.numChannels)) {
        rateGraph_ = RateGraph {};
        gainCellOversamplingFactor_ = 1;
        return;
    }

    if (gainCellOversamplingFactor_ > 1) {
        (void) setRateStageOversamplingExecution(rateGraph_, "gainCell", true);
        (void) setRateStageLatency(
            rateGraph_,
            "gainCell",
            gainCellDeltaPath_.latencySamplesExact(),
            gainCellDeltaPath_.latencySamples(),
            gainCellDeltaPath_.latencySamples());
    }

    detector_.prepare(config.sampleRate * static_cast<double>(sidechainOversamplingFactor_));
    // Prepare both engines so a later promoteToFullActiveModel() call does not
    // need to revisit the audio thread to re-arm capacitor/Newton state.
    defaultGainReductionModel_.prepare(config.sampleRate);
    fullActiveEngine_.prepare(config.sampleRate);
    tableReductionEngine_.prepare(config.sampleRate);
    if (gainReductionModel_ != &defaultGainReductionModel_
        && gainReductionModel_ != &fullActiveEngine_
        && gainReductionModel_ != &tableReductionEngine_) {
        gainReductionModel_->prepare(config.sampleRate);
    }
    prepared_ = true;
}

void U273DspEngine::reset() noexcept
{
    detector_.reset();
    gainCellDeltaPath_.reset();
    defaultGainReductionModel_.reset();
    fullActiveEngine_.reset();
    tableReductionEngine_.reset();
    if (gainReductionModel_ != &defaultGainReductionModel_
        && gainReductionModel_ != &fullActiveEngine_
        && gainReductionModel_ != &tableReductionEngine_) {
        gainReductionModel_->reset();
    }
}

bool U273DspEngine::loadReductionTable(const std::vector<TableReductionPoint>& points)
{
    if (!tableReductionEngine_.loadReductionTable(points)) {
        return false;
    }
    gainReductionModel_ = &tableReductionEngine_;
    return true;
}

bool U273DspEngine::isUsingTableReduction() const noexcept
{
    return gainReductionModel_ == &tableReductionEngine_
        && tableReductionEngine_.hasValidatedTable();
}

void U273DspEngine::promoteToFullActiveModel(
    const std::array<double, FullActiveRealtimeEngine::kCalibratedParameterCount>& parameters) noexcept
{
    fullActiveEngine_.setActiveModelParameters(parameters);
    if (fullActiveEngine_.hasParameters() && !tableReductionEngine_.hasValidatedTable()) {
        gainReductionModel_ = &fullActiveEngine_;
    }
    // On rejection the pointer is intentionally left untouched: a previously
    // valid full active promotion stays in effect, and a fresh engine keeps the
    // default analog bridge model.
}

bool U273DspEngine::isPromotedToFullActiveModel() const noexcept
{
    return gainReductionModel_ == &fullActiveEngine_ && fullActiveEngine_.hasParameters();
}

ProcessStatus U273DspEngine::process(const u273::core::ProcessContext& context,
                                     const u273::core::ParameterSnapshot& snapshot,
                                     u273::core::MeterFrame* meterFrame) noexcept
{
    if (!prepared_) {
        return ProcessStatus::notPrepared;
    }

    if (!context.isValid()) {
        return ProcessStatus::invalidContext;
    }

    if (!snapshot.isValid()) {
        return ProcessStatus::invalidSnapshot;
    }

    if (snapshot.isBypassed()) {
        // Bypass is still metered and latency-aligned with the active gain-cell
        // path, but it must not apply gain, detector, or analog-model state
        // changes to the audio stream.
        auto peak = 0.0f;
        auto outputPeak = 0.0f;
        auto clipped = false;

        for (int sample = 0; sample < context.audio.numSamples; ++sample) {
            for (int channel = 0; channel < context.audio.numChannels; ++channel) {
                const auto value = context.audio.getSample(channel, sample);
                const auto output = gainCellDeltaPath_.processSample(channel, value, 1.0f, 0.0f);
                context.audio.setSample(channel, sample, output);
                const auto absValue = std::fabs(value);
                const auto absOutput = std::fabs(output);
                peak = std::max(peak, absValue);
                outputPeak = std::max(outputPeak, absOutput);
                clipped = clipped || absOutput >= 1.0f;
            }
        }

        if (meterFrame != nullptr) {
            meterFrame->inputPeakDb = u273::core::linearToDb(peak);
            meterFrame->outputPeakDb = u273::core::linearToDb(outputPeak);
            meterFrame->gainReductionDb = 0.0f;
            meterFrame->clipFlag = clipped;
            meterFrame->sequence = context.blockSequence;
        }

        return ProcessStatus::ok;
    }

    detector_.setTimeConstants(snapshot.attackMs, snapshot.releaseMs);

    const auto inputGain = u273::core::dbToLinear(snapshot.inputGainDb);
    const auto outputGain = u273::core::dbToLinear(snapshot.outputGainDb);
    const auto wetMix = u273::core::clampFloat(snapshot.mix, 0.0f, 1.0f);

    auto inputPeak = 0.0f;
    auto outputPeak = 0.0f;
    auto maxReductionDb = 0.0f;
    auto clipped = false;

    for (int sample = 0; sample < context.audio.numSamples; ++sample) {
        // Use the loudest processed channel as the sidechain command for this
        // block-local mono detector path.
        auto sidechainPeak = 0.0f;

        for (int channel = 0; channel < context.audio.numChannels; ++channel) {
            const auto dry = context.audio.getSample(channel, sample);
            const auto driven = dry * inputGain;
            sidechainPeak = std::max(sidechainPeak, std::fabs(driven));
            inputPeak = std::max(inputPeak, std::fabs(driven));
        }

        const auto detectorInput = sidechainPeak * snapshot.detectorScale;
        auto envelope = detector_.value();
        for (auto step = 0; step < sidechainOversamplingFactor_; ++step) {
            envelope = detector_.processSample(detectorInput);
        }
        const auto reductionDb = gainReductionModel_->evaluateGainReductionDb(envelope, snapshot);
        const auto reductionGain = u273::core::dbToLinear(-reductionDb);
        const auto wetGain = inputGain * reductionGain * outputGain;

        maxReductionDb = std::max(maxReductionDb, reductionDb);

        for (int channel = 0; channel < context.audio.numChannels; ++channel) {
            const auto dry = context.audio.getSample(channel, sample);
            const auto output = gainCellDeltaPath_.processSample(channel, dry, wetGain, wetMix);
            context.audio.setSample(channel, sample, output);

            const auto absOutput = std::fabs(output);
            outputPeak = std::max(outputPeak, absOutput);
            clipped = clipped || absOutput >= 1.0f;
        }
    }

    if (meterFrame != nullptr) {
        meterFrame->inputPeakDb = u273::core::linearToDb(inputPeak);
        meterFrame->outputPeakDb = u273::core::linearToDb(outputPeak);
        meterFrame->gainReductionDb = maxReductionDb;
        meterFrame->clipFlag = clipped;
        meterFrame->sequence = context.blockSequence;
    }

    return ProcessStatus::ok;
}

} // namespace u273::dsp
