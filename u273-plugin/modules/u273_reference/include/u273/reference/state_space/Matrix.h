#pragma once

#include <cstddef>
#include <vector>

namespace u273::reference::state_space {

using Vector = std::vector<double>;

// Small dense matrix utility for offline validation. Realtime DSP never uses
// this path, so clarity is preferred over sparse-matrix complexity.
class DenseMatrix {
public:
    DenseMatrix() = default;
    DenseMatrix(std::size_t rows, std::size_t columns, double initialValue = 0.0);

    void resize(std::size_t rows, std::size_t columns, double initialValue = 0.0);
    void fill(double value);

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
    [[nodiscard]] bool isSquare() const noexcept { return rows_ == columns_; }

    [[nodiscard]] double& at(std::size_t row, std::size_t column);
    [[nodiscard]] double at(std::size_t row, std::size_t column) const;

private:
    std::size_t rows_ {};
    std::size_t columns_ {};
    std::vector<double> values_ {};
};

struct LinearSolveResult {
    bool solved {};
    double residualNorm {};
};

// One-sided Jacobi SVD result for small dense matrices. Singular values are
// returned in descending order. U is M x N, V is N x N, both with orthonormal
// columns when converged.
struct SvdResult {
    Vector singularValues {};
    DenseMatrix u {};
    DenseMatrix v {};
    int sweeps {};
    bool converged {};
};

[[nodiscard]] double infinityNorm(const Vector& values) noexcept;
[[nodiscard]] double twoNorm(const Vector& values) noexcept;

[[nodiscard]] DenseMatrix transposed(const DenseMatrix& matrix);
[[nodiscard]] Vector multiply(const DenseMatrix& matrix, const Vector& rhs);

[[nodiscard]] LinearSolveResult solveLinearSystem(DenseMatrix matrix,
                                                  Vector rhs,
                                                  Vector& solution,
                                                  double pivotFloor = 1.0e-18);

// One-sided Jacobi SVD for small matrices (column count <= 16 in practice).
// Returns converged=false if the off-diagonal threshold is not reached within
// maxSweeps; never throws.
[[nodiscard]] SvdResult jacobiSingularValues(const DenseMatrix& matrix,
                                             double tolerance = 1.0e-12,
                                             int maxSweeps = 50);

} // namespace u273::reference::state_space
