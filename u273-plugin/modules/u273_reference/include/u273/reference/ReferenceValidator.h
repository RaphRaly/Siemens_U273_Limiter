#pragma once

#include "u273/reference/ScientificReferenceModel.h"

namespace u273::reference {

// Summary of scientific comparison checks against the current guarded boundary.
struct ValidationReport {
    bool passed {};
    int comparedPoints {};
    double maxAbsErrorDb {};
    u273::core::ModelBoundary boundary {u273::core::currentScientificBoundary()};

    [[nodiscard]] bool isValid() const noexcept;
};

// Compares reference-model claims with the boundary policy expected by tests.
class ReferenceValidator {
public:
    explicit ReferenceValidator(const ScientificReferenceModel& referenceModel) noexcept;

    [[nodiscard]] ValidationReport compareGuardedBoundary() const noexcept;

private:
    const ScientificReferenceModel* referenceModel_ {};
};

} // namespace u273::reference
