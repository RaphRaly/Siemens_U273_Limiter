#pragma once

#include "u273/core/MeterFrame.h"
#include "u273/core/ParameterSnapshot.h"
#include "u273/core/ProcessContext.h"
#include "u273/dsp/AnalogRealtimeEngine.h"
#include "u273/dsp/DetectorEnvelope.h"
#include "u273/dsp/RealtimeGainReductionModel.h"

namespace u273::dsp {

struct DspPrepareConfig {
    double sampleRate {48000.0};
    int maxBlockSize {};
    int numChannels {2};

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

private:
    bool prepared_ {false};
    DspPrepareConfig config_ {};
    DetectorEnvelope detector_ {};
    // Default model keeps normal construction allocation-free; tests and future
    // variants can inject another implementation through the alternate ctor.
    AnalogRealtimeEngine defaultGainReductionModel_ {};
    RealtimeGainReductionModel* gainReductionModel_ {};
};

} // namespace u273::dsp
