#pragma once

#include <array>
#include <vector>

#include "u273/core/MeterFrame.h"
#include "u273/core/ParameterSnapshot.h"
#include "u273/core/ProcessContext.h"
#include "u273/dsp/AnalogRealtimeEngine.h"
#include "u273/dsp/DetectorEnvelope.h"
#include "u273/dsp/FullActiveRealtimeEngine.h"
#include "u273/dsp/GainCellDeltaPath.h"
#include "u273/dsp/RateGraph.h"
#include "u273/dsp/RealtimeGainReductionModel.h"
#include "u273/dsp/TableReductionRealtimeEngine.h"

namespace u273::dsp {

struct DspPrepareConfig {
    double sampleRate {48000.0};
    int maxBlockSize {};
    int numChannels {2};
    RealtimeQualityMode qualityMode {RealtimeQualityMode::precise};

    [[nodiscard]] bool isValid() const noexcept;
};

// Audio-block status values are explicit so the host adapter can fail quietly
// without exceptions on the realtime thread.
enum class ProcessStatus {
    ok = 0,
    notPrepared,
    invalidContext,
    invalidSnapshot
};

// Realtime DSP orchestrator: validates contracts, computes detector state,
// delegates gain reduction, writes audio, and publishes one meter frame.
class U273DspEngine {
public:
    U273DspEngine() noexcept;
    explicit U273DspEngine(RealtimeGainReductionModel& gainReductionModel) noexcept;

    void prepare(const DspPrepareConfig& config) noexcept;
    void reset() noexcept;

    [[nodiscard]] ProcessStatus process(const u273::core::ProcessContext& context,
                                        const u273::core::ParameterSnapshot& snapshot,
                                        u273::core::MeterFrame* meterFrame) noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept { return gainReductionModel_->boundary(); }
    [[nodiscard]] const RateGraph& rateGraph() const noexcept { return rateGraph_; }
    [[nodiscard]] int latencySamples() const noexcept { return totalLatencySamples(rateGraph_); }
    [[nodiscard]] RealtimeQualityMode qualityMode() const noexcept { return config_.qualityMode; }
    [[nodiscard]] int gainCellOversamplingFactor() const noexcept { return gainCellOversamplingFactor_; }
    [[nodiscard]] bool oversamplingExecutionEnabled() const noexcept
    {
        return rateGraph_.oversamplingExecutionEnabled;
    }

    // Loads an offline-validated monotone reduction table. Called off the audio
    // thread; if validation succeeds the DSP switches from the surrogate path
    // to the guarded table engine. Rejected tables leave the current model in
    // place.
    [[nodiscard]] bool loadReductionTable(const std::vector<TableReductionPoint>& points);
    [[nodiscard]] bool isUsingTableReduction() const noexcept;

    // Legacy experimental full-active hook. It remains available for lab
    // comparisons, but the boundary stays FULL_ACTIVE_MODEL_UNVERIFIED until
    // the strict device-output audio gate exists.
    void promoteToFullActiveModel(
        const std::array<double, FullActiveRealtimeEngine::kCalibratedParameterCount>& parameters) noexcept;

    [[nodiscard]] bool isPromotedToFullActiveModel() const noexcept;

    [[nodiscard]] const FullActiveRealtimeEngine& fullActiveEngine() const noexcept { return fullActiveEngine_; }
    [[nodiscard]] const TableReductionRealtimeEngine& tableReductionEngine() const noexcept
    {
        return tableReductionEngine_;
    }

private:
    bool prepared_ {false};
    DspPrepareConfig config_ {};
    RateGraph rateGraph_ {};
    int sidechainOversamplingFactor_ {1};
    int gainCellOversamplingFactor_ {1};
    DetectorEnvelope detector_ {};
    GainCellDeltaPath gainCellDeltaPath_ {};
    // Default model keeps normal construction allocation-free; tests and future
    // variants can inject another implementation through the alternate ctor.
    AnalogRealtimeEngine defaultGainReductionModel_ {};
    FullActiveRealtimeEngine fullActiveEngine_ {};
    TableReductionRealtimeEngine tableReductionEngine_ {};
    RealtimeGainReductionModel* gainReductionModel_ {};
};

} // namespace u273::dsp
