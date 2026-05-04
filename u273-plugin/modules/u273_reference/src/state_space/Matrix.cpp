#include "u273/reference/state_space/Matrix.h"

#include <algorithm>
#include <cmath>

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

} // namespace u273::reference::state_space
