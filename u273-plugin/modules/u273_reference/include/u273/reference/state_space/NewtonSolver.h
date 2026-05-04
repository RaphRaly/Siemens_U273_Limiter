#pragma once

#include <functional>

#include "u273/reference/state_space/Matrix.h"

namespace u273::reference::state_space {

struct NewtonOptions {
    int maxIterations {32};
    double residualTolerance {1.0e-10};
    double deltaTolerance {1.0e-12};
    double minDamping {1.0 / 1024.0};
    double pivotFloor {1.0e-18};
};

struct NewtonResult {
    bool converged {};
    bool linearSolveFailed {};
    int iterations {};
    double residualNorm {};
    double deltaNorm {};
};

using ResidualJacobianFunction = std::function<void(const Vector& unknowns,
                                                    Vector& residual,
                                                    DenseMatrix& jacobian)>;

// Damped Newton solver for offline nonlinear circuit steps. The caller supplies
// residual and Jacobian assembly so device stamping stays outside the solver.
class NewtonSolver {
public:
    explicit NewtonSolver(NewtonOptions options = {});

    [[nodiscard]] NewtonResult solve(Vector& unknowns,
                                     const ResidualJacobianFunction& residualJacobian) const;

private:
    NewtonOptions options_ {};
};

} // namespace u273::reference::state_space
