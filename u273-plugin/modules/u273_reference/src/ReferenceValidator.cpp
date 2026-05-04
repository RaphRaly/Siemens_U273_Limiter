#include "u273/reference/ReferenceValidator.h"

namespace u273::reference {

bool ValidationReport::isValid() const noexcept
{
    return comparedPoints >= 0
        && maxAbsErrorDb >= 0.0
        && boundary != u273::core::ModelBoundary::unknown;
}

ReferenceValidator::ReferenceValidator(const ScientificReferenceModel& referenceModel) noexcept
    : referenceModel_(&referenceModel)
{
}

ValidationReport ReferenceValidator::compareGuardedBoundary() const noexcept
{
    ValidationReport report {};
    report.boundary = referenceModel_ != nullptr
        ? referenceModel_->boundary()
        : u273::core::ModelBoundary::unknown;
    report.comparedPoints = 0;
    report.maxAbsErrorDb = 0.0;
    report.passed = report.boundary == u273::core::ModelBoundary::passWithGuardedBoundaries;
    return report;
}

} // namespace u273::reference
