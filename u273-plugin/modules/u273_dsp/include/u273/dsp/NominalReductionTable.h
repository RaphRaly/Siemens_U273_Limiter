#pragma once

#include <vector>

#include "u273/dsp/TableReductionRealtimeEngine.h"

namespace u273::dsp {

[[nodiscard]] std::vector<TableReductionPoint> makeNominalU273ReductionTable();

} // namespace u273::dsp
