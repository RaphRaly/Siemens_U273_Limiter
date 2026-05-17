#include "u273/dsp/TableReductionRealtimeEngine.h"

#include <algorithm>
#include <cmath>

namespace u273::dsp {

namespace {

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] float clampFinite(float value, float lower, float upper) noexcept
{
    if (!finite(value)) {
        return value < 0.0f ? lower : upper;
    }
    return std::clamp(value, lower, upper);
}

} // namespace

bool TableReductionFrame::isValid() const noexcept
{
    return finite(commandVolt)
        && commandVolt >= 0.0f
        && finite(gainReductionDb)
        && gainReductionDb >= 0.0f
        && sampleCount >= 0;
}

void TableReductionRealtimeEngine::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(1.0, sampleRate);
    reset();
}

void TableReductionRealtimeEngine::reset() noexcept
{
    sampleCount_ = 0;
    lastFrame_ = TableReductionFrame {};
}

bool TableReductionRealtimeEngine::tableIsValid(const std::vector<TableReductionPoint>& points) noexcept
{
    if (points.size() < 2) {
        return false;
    }

    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        if (!finite(point.commandVolt)
            || !finite(point.gainReductionDb)
            || !finite(point.dGainReductionDbDCommand)
            || point.commandVolt < 0.0f
            || point.gainReductionDb < 0.0f) {
            return false;
        }

        if (index > 0) {
            const auto& previous = points[index - 1];
            if (!(point.commandVolt > previous.commandVolt)) {
                return false;
            }
            if (point.gainReductionDb + 1.0e-5f < previous.gainReductionDb) {
                return false;
            }
        }
    }

    return true;
}

bool TableReductionRealtimeEngine::loadReductionTable(const std::vector<TableReductionPoint>& points)
{
    if (!tableIsValid(points)) {
        return false;
    }

    points_ = points;
    tableValid_ = true;
    reset();
    return true;
}

float TableReductionRealtimeEngine::commandFromEnvelope(
    float detectorEnvelope,
    const u273::core::ParameterSnapshot& snapshot) const noexcept
{
    (void) sampleRate_;

    if (!finite(detectorEnvelope)) {
        if (!points_.empty() && !std::signbit(detectorEnvelope)) {
            return points_.back().commandVolt;
        }
        return 0.0f;
    }
    if (detectorEnvelope <= 0.0f) {
        return 0.0f;
    }

    const auto drive = std::clamp(snapshot.drive, 0.0f, 1.0f);
    const auto commandScale = 0.25f + 0.75f * drive;
    const auto command = detectorEnvelope * commandScale;
    if (points_.empty()) {
        return std::max(0.0f, command);
    }
    return clampFinite(command, points_.front().commandVolt, points_.back().commandVolt);
}

float TableReductionRealtimeEngine::lookup(float commandVolt) const noexcept
{
    if (!tableValid_ || points_.empty()) {
        return 0.0f;
    }

    if (commandVolt <= points_.front().commandVolt) {
        return points_.front().gainReductionDb;
    }
    if (commandVolt >= points_.back().commandVolt) {
        return points_.back().gainReductionDb;
    }

    auto upper = std::lower_bound(
        points_.begin(),
        points_.end(),
        commandVolt,
        [](const TableReductionPoint& point, float value) {
            return point.commandVolt < value;
        });

    if (upper == points_.begin()) {
        return upper->gainReductionDb;
    }
    if (upper == points_.end()) {
        return points_.back().gainReductionDb;
    }

    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const auto dx = right.commandVolt - left.commandVolt;
    if (!(dx > 0.0f) || !finite(dx)) {
        return left.gainReductionDb;
    }

    const auto t = std::clamp((commandVolt - left.commandVolt) / dx, 0.0f, 1.0f);
    const auto reduction = left.gainReductionDb
        + t * (right.gainReductionDb - left.gainReductionDb);
    return finite(reduction) ? std::max(0.0f, reduction) : 0.0f;
}

float TableReductionRealtimeEngine::evaluateGainReductionDb(
    float detectorEnvelope,
    const u273::core::ParameterSnapshot& snapshot) noexcept
{
    ++sampleCount_;

    const auto command = commandFromEnvelope(detectorEnvelope, snapshot);
    const auto reduction = lookup(command);
    lastFrame_ = TableReductionFrame {
        command,
        reduction,
        sampleCount_};
    return reduction;
}

} // namespace u273::dsp
