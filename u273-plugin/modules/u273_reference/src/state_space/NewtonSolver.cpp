#include "u273/reference/state_space/NewtonSolver.h"

#include <algorithm>
#include <cmath>

namespace u273::reference::state_space {

namespace {

bool isFiniteVector(const Vector& values) noexcept
{
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

Vector scaledStep(const Vector& origin, const Vector& step, double alpha)
{
    Vector trial = origin;
    for (std::size_t index = 0; index < trial.size(); ++index) {
        trial[index] += alpha * step[index];
    }
    return trial;
}

} // namespace

NewtonSolver::NewtonSolver(NewtonOptions options)
    : options_(options)
{
}

NewtonResult NewtonSolver::solve(Vector& unknowns,
                                 const ResidualJacobianFunction& residualJacobian) const
{
    NewtonResult result {};
    if (unknowns.empty() || options_.maxIterations <= 0) {
        return result;
    }

    Vector residual {};
    DenseMatrix jacobian {};

    for (int iteration = 0; iteration < options_.maxIterations; ++iteration) {
        residualJacobian(unknowns, residual, jacobian);
        result.residualNorm = infinityNorm(residual);
        result.iterations = iteration + 1;

        if (!isFiniteVector(residual)) {
            return result;
        }

        if (result.residualNorm <= options_.residualTolerance) {
            result.converged = true;
            return result;
        }

        Vector rhs = residual;
        for (auto& value : rhs) {
            value = -value;
        }

        Vector step {};
        const auto linear = solveLinearSystem(jacobian, rhs, step, options_.pivotFloor);
        if (!linear.solved || !isFiniteVector(step)) {
            result.linearSolveFailed = true;
            return result;
        }

        result.deltaNorm = infinityNorm(step);
        if (result.deltaNorm <= options_.deltaTolerance) {
            result.converged = true;
            return result;
        }

        auto acceptedUnknowns = unknowns;
        auto acceptedResidualNorm = result.residualNorm;
        auto alpha = 1.0;

        // Backtracking damping keeps nonlinear device steps from overshooting
        // when the first full Newton step increases the residual.
        while (alpha >= options_.minDamping) {
            auto trial = scaledStep(unknowns, step, alpha);
            Vector trialResidual {};
            DenseMatrix trialJacobian {};
            residualJacobian(trial, trialResidual, trialJacobian);
            const auto trialNorm = infinityNorm(trialResidual);

            if (isFiniteVector(trialResidual) && trialNorm <= acceptedResidualNorm) {
                acceptedUnknowns = std::move(trial);
                acceptedResidualNorm = trialNorm;
                break;
            }

            alpha *= 0.5;
        }

        unknowns = std::move(acceptedUnknowns);
        result.residualNorm = acceptedResidualNorm;
    }

    result.converged = result.residualNorm <= options_.residualTolerance;
    return result;
}

} // namespace u273::reference::state_space
