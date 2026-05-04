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

[[nodiscard]] double infinityNorm(const Vector& values) noexcept;

[[nodiscard]] LinearSolveResult solveLinearSystem(DenseMatrix matrix,
                                                  Vector rhs,
                                                  Vector& solution,
                                                  double pivotFloor = 1.0e-18);

} // namespace u273::reference::state_space
