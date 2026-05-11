#include "u273/reference/calibration/ActiveTopologyCandidate.h"

#include <utility>

namespace u273::reference::calibration {

bool ActiveTopologyCandidate::hasUsablePins() const noexcept
{
    return kind != ActiveDeviceKind::unknown
        && collector.value > 0
        && base.value > 0
        && emitter.value > 0
        && collector != base
        && collector != emitter
        && base != emitter;
}

void ActiveTopologyCandidate::reject(std::string reason)
{
    rejectionReason = std::move(reason);
}

} // namespace u273::reference::calibration
