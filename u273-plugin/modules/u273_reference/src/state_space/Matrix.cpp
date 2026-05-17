#include "u273/reference/state_space/Matrix.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace u273::reference::state_space {

DenseMatrix::DenseMatrix(std::size_t rows, std::size_t columns, double initialValue)
{
    resize(rows, columns, initialValue);
}

void DenseMatrix::resize(std::size_t rows, std::size_t columns, double initialValue)
{
    rows_ = rows;
    columns_ = columns;
    values_.assign(rows * columns, initialValue);
}

void DenseMatrix::fill(double value)
{
    std::fill(values_.begin(), values_.end(), value);
}

double& DenseMatrix::at(std::size_t row, std::size_t column)
{
    return values_.at(row * columns_ + column);
}

double DenseMatrix::at(std::size_t row, std::size_t column) const
{
    return values_.at(row * columns_ + column);
}

double infinityNorm(const Vector& values) noexcept
{
    double norm {};
    for (const auto value : values) {
        norm = std::max(norm, std::fabs(value));
    }
    return norm;
}

double twoNorm(const Vector& values) noexcept
{
    // Scaled algorithm: divide by max abs to avoid overflow / underflow on
    // extreme magnitudes before squaring.
    const auto scale = infinityNorm(values);
    if (scale == 0.0) {
        return 0.0;
    }
    double accumulator {};
    for (const auto value : values) {
        const auto scaled = value / scale;
        accumulator += scaled * scaled;
    }
    return scale * std::sqrt(accumulator);
}

DenseMatrix transposed(const DenseMatrix& matrix)
{
    DenseMatrix result {matrix.columns(), matrix.rows()};
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        for (std::size_t column = 0; column < matrix.columns(); ++column) {
            result.at(column, row) = matrix.at(row, column);
        }
    }
    return result;
}

Vector multiply(const DenseMatrix& matrix, const Vector& rhs)
{
    Vector result {};
    if (matrix.columns() != rhs.size()) {
        return result;
    }
    result.assign(matrix.rows(), 0.0);
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        double accumulator {};
        for (std::size_t column = 0; column < matrix.columns(); ++column) {
            accumulator += matrix.at(row, column) * rhs[column];
        }
        result[row] = accumulator;
    }
    return result;
}

LinearSolveResult solveLinearSystem(DenseMatrix matrix,
                                    Vector rhs,
                                    Vector& solution,
                                    double pivotFloor)
{
    // Dense Gaussian elimination is sufficient for the small guarded fixtures
    // and makes numerical failures easy to inspect in tests.
    const auto size = matrix.rows();
    LinearSolveResult result {};
    solution.assign(size, 0.0);

    if (!matrix.isSquare() || matrix.columns() != size || rhs.size() != size) {
        return result;
    }

    const auto originalMatrix = matrix;
    const auto originalRhs = rhs;

    for (std::size_t column = 0; column < size; ++column) {
        std::size_t pivotRow = column;
        auto pivotMagnitude = std::fabs(matrix.at(column, column));

        for (std::size_t row = column + 1; row < size; ++row) {
            const auto candidate = std::fabs(matrix.at(row, column));
            if (candidate > pivotMagnitude) {
                pivotMagnitude = candidate;
                pivotRow = row;
            }
        }

        if (pivotMagnitude <= pivotFloor) {
            return result;
        }

        if (pivotRow != column) {
            for (std::size_t swapColumn = column; swapColumn < size; ++swapColumn) {
                std::swap(matrix.at(column, swapColumn), matrix.at(pivotRow, swapColumn));
            }
            std::swap(rhs[column], rhs[pivotRow]);
        }

        const auto pivot = matrix.at(column, column);
        for (std::size_t row = column + 1; row < size; ++row) {
            const auto factor = matrix.at(row, column) / pivot;
            matrix.at(row, column) = 0.0;

            for (std::size_t eliminationColumn = column + 1; eliminationColumn < size; ++eliminationColumn) {
                matrix.at(row, eliminationColumn) -= factor * matrix.at(column, eliminationColumn);
            }

            rhs[row] -= factor * rhs[column];
        }
    }

    for (std::size_t rowOffset = 0; rowOffset < size; ++rowOffset) {
        const auto row = size - 1 - rowOffset;
        auto sum = rhs[row];

        for (std::size_t column = row + 1; column < size; ++column) {
            sum -= matrix.at(row, column) * solution[column];
        }

        const auto diagonal = matrix.at(row, row);
        if (std::fabs(diagonal) <= pivotFloor) {
            return result;
        }

        solution[row] = sum / diagonal;
    }

    Vector residual(size, 0.0);
    for (std::size_t row = 0; row < size; ++row) {
        auto value = -originalRhs[row];
        for (std::size_t column = 0; column < size; ++column) {
            value += originalMatrix.at(row, column) * solution[column];
        }
        residual[row] = value;
    }

    result.solved = true;
    result.residualNorm = infinityNorm(residual);
    return result;
}

SvdResult jacobiSingularValues(const DenseMatrix& matrix, double tolerance, int maxSweeps)
{
    SvdResult result {};
    const auto m = matrix.rows();
    const auto n = matrix.columns();
    if (m == 0 || n == 0) {
        result.converged = true;
        return result;
    }

    // One-sided Jacobi: rotate column pairs of A until off-diagonal of A^T A
    // is below tolerance. Accumulate the rotations in V so that A_initial = U S V^T.
    DenseMatrix work = matrix;
    DenseMatrix v {n, n};
    for (std::size_t i = 0; i < n; ++i) {
        v.at(i, i) = 1.0;
    }

    int sweep = 0;
    for (; sweep < maxSweeps; ++sweep) {
        double offMax {};
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                double alpha {};
                double beta {};
                double gamma {};
                for (std::size_t i = 0; i < m; ++i) {
                    const auto ap = work.at(i, p);
                    const auto aq = work.at(i, q);
                    alpha += ap * ap;
                    beta += aq * aq;
                    gamma += ap * aq;
                }

                const auto scale = std::sqrt(alpha * beta);
                if (scale == 0.0) {
                    continue;
                }

                const auto relative = std::fabs(gamma) / scale;
                if (relative > offMax) {
                    offMax = relative;
                }
                if (relative <= tolerance) {
                    continue;
                }

                // Solve t^2 + 2 zeta t - 1 = 0 for the smaller root in magnitude.
                const auto zeta = (beta - alpha) / (2.0 * gamma);
                const auto t = (zeta >= 0.0)
                    ? 1.0 / (zeta + std::sqrt(1.0 + zeta * zeta))
                    : 1.0 / (zeta - std::sqrt(1.0 + zeta * zeta));
                const auto c = 1.0 / std::sqrt(1.0 + t * t);
                const auto s = t * c;

                for (std::size_t i = 0; i < m; ++i) {
                    const auto ap = work.at(i, p);
                    const auto aq = work.at(i, q);
                    work.at(i, p) = c * ap - s * aq;
                    work.at(i, q) = s * ap + c * aq;
                }
                for (std::size_t i = 0; i < n; ++i) {
                    const auto vp = v.at(i, p);
                    const auto vq = v.at(i, q);
                    v.at(i, p) = c * vp - s * vq;
                    v.at(i, q) = s * vp + c * vq;
                }
            }
        }

        if (offMax <= tolerance) {
            ++sweep;
            result.converged = true;
            break;
        }
    }

    Vector singularValues(n, 0.0);
    DenseMatrix u {m, n};
    for (std::size_t i = 0; i < n; ++i) {
        double normSq {};
        for (std::size_t j = 0; j < m; ++j) {
            const auto value = work.at(j, i);
            normSq += value * value;
        }
        const auto sigma = std::sqrt(normSq);
        singularValues[i] = sigma;
        if (sigma > 0.0) {
            for (std::size_t j = 0; j < m; ++j) {
                u.at(j, i) = work.at(j, i) / sigma;
            }
        }
    }

    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t {});
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return singularValues[left] > singularValues[right];
    });

    Vector sortedSingularValues(n, 0.0);
    DenseMatrix sortedU {m, n};
    DenseMatrix sortedV {n, n};
    for (std::size_t i = 0; i < n; ++i) {
        const auto source = order[i];
        sortedSingularValues[i] = singularValues[source];
        for (std::size_t j = 0; j < m; ++j) {
            sortedU.at(j, i) = u.at(j, source);
        }
        for (std::size_t j = 0; j < n; ++j) {
            sortedV.at(j, i) = v.at(j, source);
        }
    }

    result.singularValues = std::move(sortedSingularValues);
    result.u = std::move(sortedU);
    result.v = std::move(sortedV);
    result.sweeps = sweep;
    return result;
}

} // namespace u273::reference::state_space
