#pragma once

#include "u273/core/ModelBoundary.h"
#include "u273/core/ParameterSnapshot.h"

namespace u273::reference {

enum class AnalysisKind {
    dc = 0,
    ac,
    transient
};

// Offline scenario descriptor shared by DC/AC/transient reference paths.
struct ReferenceScenario {
    AnalysisKind kind {AnalysisKind::dc};
    double inputLevelDbu {};
    double frequencyHz {1000.0};
    double durationSeconds {0.1};
    u273::core::ParameterSnapshot parameters {};

    [[nodiscard]] bool isValid() const noexcept;
};

// Reference solver diagnostics. These values are intentionally visible because
// guarded scientific status depends on convergence and residuals.
struct ReferenceSolveResult {
    u273::core::ModelBoundary boundary {u273::core::currentScientificBoundary()};
    double residualNorm {};
    int iterations {};
    double outputLevelDbu {};
    bool converged {true};

    [[nodiscard]] bool isValid() const noexcept;
};

// Lightweight reference facade used by validation tests. It reports boundary
// confidence separately from the realtime DSP implementation.
class ScientificReferenceModel {
public:
    [[nodiscard]] ReferenceSolveResult solveDc(const ReferenceScenario& scenario) const noexcept;
    [[nodiscard]] ReferenceSolveResult solveAc(const ReferenceScenario& scenario) const noexcept;
    [[nodiscard]] ReferenceSolveResult solveTransient(const ReferenceScenario& scenario) const noexcept;

    [[nodiscard]] u273::core::ModelBoundary boundary() const noexcept
    {
        return u273::core::currentScientificBoundary();
    }
};

} // namespace u273::reference
