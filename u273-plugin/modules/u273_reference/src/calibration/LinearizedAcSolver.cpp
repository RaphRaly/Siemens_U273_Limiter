#include "u273/reference/calibration/LinearizedAcSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

using Complex = std::complex<double>;
using ComplexVector = std::vector<Complex>;

class ComplexMatrix {
public:
    ComplexMatrix(std::size_t rows, std::size_t columns)
        : rows_(rows)
        , columns_(columns)
        , values_(rows * columns)
    {
    }

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    [[nodiscard]] Complex& at(std::size_t row, std::size_t column)
    {
        return values_[row * columns_ + column];
    }

    [[nodiscard]] Complex at(std::size_t row, std::size_t column) const
    {
        return values_[row * columns_ + column];
    }

private:
    std::size_t rows_ {};
    std::size_t columns_ {};
    std::vector<Complex> values_ {};
};

[[nodiscard]] int nodeUnknownIndex(const ss::CircuitGraph& circuit, ss::NodeId node) noexcept
{
    if (node.value <= 0 || node.value >= circuit.nodeCount()) {
        return -1;
    }
    return node.value - 1;
}

[[nodiscard]] int voltageSourceUnknownIndex(const ss::CircuitGraph& circuit, std::size_t sourceIndex) noexcept
{
    return circuit.nodeCount() - 1 + static_cast<int>(sourceIndex);
}

[[nodiscard]] double voltageFromState(const ss::CircuitGraph& circuit,
                                      const ss::CircuitState& state,
                                      ss::NodeId node) noexcept
{
    const auto index = nodeUnknownIndex(circuit, node);
    if (index < 0 || index >= static_cast<int>(state.unknowns.size())) {
        return 0.0;
    }
    return state.unknowns[static_cast<std::size_t>(index)];
}

void addNodeMatrix(ComplexMatrix& matrix,
                   const ss::CircuitGraph& circuit,
                   ss::NodeId rowNode,
                   ss::NodeId columnNode,
                   Complex value)
{
    const auto row = nodeUnknownIndex(circuit, rowNode);
    const auto column = nodeUnknownIndex(circuit, columnNode);
    if (row >= 0 && column >= 0) {
        matrix.at(static_cast<std::size_t>(row), static_cast<std::size_t>(column)) += value;
    }
}

void stampAdmittance(ComplexMatrix& matrix,
                     const ss::CircuitGraph& circuit,
                     ss::NodeId positive,
                     ss::NodeId negative,
                     Complex admittance)
{
    addNodeMatrix(matrix, circuit, positive, positive, admittance);
    addNodeMatrix(matrix, circuit, positive, negative, -admittance);
    addNodeMatrix(matrix, circuit, negative, positive, -admittance);
    addNodeMatrix(matrix, circuit, negative, negative, admittance);
}

[[nodiscard]] bool solveComplexSystem(ComplexMatrix matrix,
                                      ComplexVector rhs,
                                      ComplexVector& solution,
                                      double pivotFloor = 1.0e-18)
{
    if (matrix.rows() != matrix.columns() || matrix.rows() != rhs.size()) {
        return false;
    }

    const auto size = matrix.rows();
    for (std::size_t column = 0; column < size; ++column) {
        auto pivotRow = column;
        auto pivotAbs = std::abs(matrix.at(column, column));
        for (auto row = column + 1; row < size; ++row) {
            const auto candidate = std::abs(matrix.at(row, column));
            if (candidate > pivotAbs) {
                pivotAbs = candidate;
                pivotRow = row;
            }
        }

        if (pivotAbs < pivotFloor) {
            return false;
        }

        if (pivotRow != column) {
            for (auto swapColumn = column; swapColumn < size; ++swapColumn) {
                std::swap(matrix.at(column, swapColumn), matrix.at(pivotRow, swapColumn));
            }
            std::swap(rhs[column], rhs[pivotRow]);
        }

        const auto pivot = matrix.at(column, column);
        for (auto row = column + 1; row < size; ++row) {
            const auto factor = matrix.at(row, column) / pivot;
            matrix.at(row, column) = {};
            for (auto updateColumn = column + 1; updateColumn < size; ++updateColumn) {
                matrix.at(row, updateColumn) -= factor * matrix.at(column, updateColumn);
            }
            rhs[row] -= factor * rhs[column];
        }
    }

    solution.assign(size, {});
    for (auto row = static_cast<int>(size) - 1; row >= 0; --row) {
        auto sum = rhs[static_cast<std::size_t>(row)];
        for (auto column = static_cast<std::size_t>(row + 1); column < size; ++column) {
            sum -= matrix.at(static_cast<std::size_t>(row), column) * solution[column];
        }
        solution[static_cast<std::size_t>(row)] = sum / matrix.at(static_cast<std::size_t>(row),
                                                                  static_cast<std::size_t>(row));
    }

    return true;
}

[[nodiscard]] bool sourceExists(const ss::CircuitGraph& circuit, const std::string& sourceId) noexcept
{
    for (const auto& source : circuit.voltageSources()) {
        if (source.id == sourceId) {
            return true;
        }
    }
    return false;
}

} // namespace

AcLinearizationResult LinearizedAcSolver::solveSmallSignal(
    const ss::CircuitGraph& circuit,
    const OperatingPointResult& operatingPoint,
    const AcSourcePort& source,
    const std::vector<double>& frequenciesHz) const
{
    AcLinearizationResult result {};
    result.validInput = operatingPoint.converged
        && operatingPoint.state.unknowns.size() == static_cast<std::size_t>(circuit.unknownCount())
        && source.outputNode.value > 0
        && source.outputNode.value < circuit.nodeCount()
        && source.amplitudeVolt > 0.0
        && std::isfinite(source.amplitudeVolt)
        && sourceExists(circuit, source.voltageSourceId)
        && !frequenciesHz.empty();

    if (!result.validInput) {
        result.failures.push_back("AC linearization requires a converged OP, source, output node and frequencies");
        return result;
    }

    constexpr auto pi = 3.1415926535897932384626433832795;
    const auto unknownCount = static_cast<std::size_t>(circuit.unknownCount());
    const auto outputIndex = nodeUnknownIndex(circuit, source.outputNode);

    result.points.reserve(frequenciesHz.size());
    for (const auto frequency : frequenciesHz) {
        if (!std::isfinite(frequency) || frequency <= 0.0) {
            result.failures.push_back("AC frequency must be finite and positive");
            return result;
        }

        ComplexMatrix matrix {unknownCount, unknownCount};
        ComplexVector rhs(unknownCount, Complex {});

        for (const auto& resistor : circuit.resistors()) {
            if (resistor.resistanceOhm > 0.0) {
                stampAdmittance(matrix, circuit, resistor.positive, resistor.negative, 1.0 / resistor.resistanceOhm);
            }
        }

        const auto jw = Complex {0.0, 2.0 * pi * frequency};
        for (const auto& capacitor : circuit.capacitors()) {
            if (capacitor.capacitanceFarad > 0.0) {
                stampAdmittance(matrix, circuit, capacitor.positive, capacitor.negative, jw * capacitor.capacitanceFarad);
            }
        }

        for (const auto& diode : circuit.diodes()) {
            const auto voltage = voltageFromState(circuit, operatingPoint.state, diode.anode)
                - voltageFromState(circuit, operatingPoint.state, diode.cathode);
            stampAdmittance(matrix,
                            circuit,
                            diode.anode,
                            diode.cathode,
                            diode.model.conductanceSiemens(voltage));
        }

        for (const auto& bjt : circuit.npnBjts()) {
            const auto evaluation = evaluateNpnEbersMoll(
                bjt.model,
                voltageFromState(circuit, operatingPoint.state, bjt.collector),
                voltageFromState(circuit, operatingPoint.state, bjt.base),
                voltageFromState(circuit, operatingPoint.state, bjt.emitter));
            const std::array<ss::NodeId, 3> terminals {bjt.collector, bjt.base, bjt.emitter};
            for (std::size_t row = 0; row < terminals.size(); ++row) {
                for (std::size_t column = 0; column < terminals.size(); ++column) {
                    addNodeMatrix(matrix,
                                  circuit,
                                  terminals[row],
                                  terminals[column],
                                  evaluation.dCurrentDVoltage[row][column]);
                }
            }
        }

        for (std::size_t index = 0; index < circuit.voltageSources().size(); ++index) {
            const auto& voltageSource = circuit.voltageSources()[index];
            const auto currentIndex = static_cast<std::size_t>(voltageSourceUnknownIndex(circuit, index));
            const auto positiveRow = nodeUnknownIndex(circuit, voltageSource.positive);
            const auto negativeRow = nodeUnknownIndex(circuit, voltageSource.negative);
            if (positiveRow >= 0) {
                matrix.at(static_cast<std::size_t>(positiveRow), currentIndex) += 1.0;
            }
            if (negativeRow >= 0) {
                matrix.at(static_cast<std::size_t>(negativeRow), currentIndex) -= 1.0;
            }

            const auto equationRow = currentIndex;
            const auto positiveColumn = nodeUnknownIndex(circuit, voltageSource.positive);
            const auto negativeColumn = nodeUnknownIndex(circuit, voltageSource.negative);
            if (positiveColumn >= 0) {
                matrix.at(equationRow, static_cast<std::size_t>(positiveColumn)) += 1.0;
            }
            if (negativeColumn >= 0) {
                matrix.at(equationRow, static_cast<std::size_t>(negativeColumn)) -= 1.0;
            }
            rhs[equationRow] = voltageSource.id == source.voltageSourceId ? source.amplitudeVolt : 0.0;
        }

        ComplexVector solution {};
        if (!solveComplexSystem(matrix, rhs, solution)) {
            result.failures.push_back("AC linear system did not solve");
            return result;
        }

        const auto output = outputIndex >= 0 ? solution[static_cast<std::size_t>(outputIndex)] : Complex {};
        const auto normalized = output / source.amplitudeVolt;
        const auto magnitude = std::abs(normalized);
        result.points.push_back(AcLinearizationPoint {
            frequency,
            magnitude,
            20.0 * std::log10(std::max(magnitude, 1.0e-30)),
            std::arg(normalized)});
    }

    result.solved = true;
    return result;
}

} // namespace u273::reference::calibration
