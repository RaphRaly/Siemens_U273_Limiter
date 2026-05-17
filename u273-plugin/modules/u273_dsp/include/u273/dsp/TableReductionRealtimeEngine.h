#pragma once

#include <cstddef>
#include <vector>

#include "u273/dsp/RealtimeGainReductionModel.h"

namespace u273::dsp {

struct TableReductionPoint {
    float commandVolt {};
    float gainReductionDb {};
    float dGainReductionDbDCommand {};
};

struct TableReductionFrame {
    float commandVolt {};
    float gainReductionDb {};
    int sampleCount {};

    [[nodiscard]] bool isValid() const noexcept;
};

class TableReductionRealtimeEngine final : public RealtimeGainReductionModel {
public:
    void prepare(double sampleRate) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] bool loadReductionTable(const std::vector<TableReductionPoint>& points);
    [[nodiscard]] bool hasValidatedTable() const noexcept { return tableValid_; }
    [[nodiscard]] std::size_t tableSize() const noexcept { return points_.size(); }

    [[nodiscard]] float evaluateGainReductionDb(
        float detectorEnvelope,
        const u273::core::ParameterSnapshot& snapshot) noexcept override;

    [[nodiscard]] const TableReductionFrame& lastFrame() const noexcept { return lastFrame_; }

    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept override
    {
        return tableValid_
            ? u273::core::ModelBoundary::guardedRealtimeSurrogate
            : u273::core::ModelBoundary::fullActiveModelUnverified;
    }

private:
    [[nodiscard]] static bool tableIsValid(const std::vector<TableReductionPoint>& points) noexcept;
    [[nodiscard]] float commandFromEnvelope(float detectorEnvelope,
                                            const u273::core::ParameterSnapshot& snapshot) const noexcept;
    [[nodiscard]] float lookup(float commandVolt) const noexcept;

    double sampleRate_ {48000.0};
    std::vector<TableReductionPoint> points_ {};
    bool tableValid_ {};
    int sampleCount_ {};
    TableReductionFrame lastFrame_ {};
};

} // namespace u273::dsp
