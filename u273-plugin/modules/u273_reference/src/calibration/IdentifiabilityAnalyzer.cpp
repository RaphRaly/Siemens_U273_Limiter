#include "u273/reference/calibration/IdentifiabilityAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "u273/reference/state_space/Matrix.h"

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

[[nodiscard]] double columnTwoNorm(const ss::DenseMatrix& matrix, std::size_t column)
{
    double accumulator {};
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        const auto value = matrix.at(row, column);
        accumulator += value * value;
    }
    return std::sqrt(accumulator);
}

[[nodiscard]] double dotColumns(const ss::DenseMatrix& matrix, std::size_t lhs, std::size_t rhs)
{
    double accumulator {};
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
        accumulator += matrix.at(row, lhs) * matrix.at(row, rhs);
    }
    return accumulator;
}

[[nodiscard]] IdentifiabilityResult evaluateFromSensitivity(
    const std::vector<std::string>& parameterNames,
    const ss::DenseMatrix& sensitivity,
    const std::vector<std::string>& parametersOnBound,
    const IdentifiabilityOptions& options)
{
    IdentifiabilityResult result {};
    result.parametersOnBound = parametersOnBound;
    result.validInput = sensitivity.columns() == parameterNames.size()
        && parameterNames.size() > 0
        && options.perturbationRelative > 0.0
        && options.minimumSensitivity > 0.0
        && options.conditionNumberMax > 0.0
        && options.strongCorrelationThreshold > 0.0
        && options.strongCorrelationThreshold <= 1.0;
    if (!result.validInput) {
        result.failures.push_back("invalid identifiability input");
        return result;
    }

    if (sensitivity.rows() == 0) {
        result.failures.push_back("sensitivity matrix has zero rows; need at least one residual");
        return result;
    }

    std::vector<double> columnNorms(parameterNames.size(), 0.0);
    for (std::size_t index = 0; index < parameterNames.size(); ++index) {
        columnNorms[index] = columnTwoNorm(sensitivity, index);
        if (columnNorms[index] < options.minimumSensitivity) {
            result.weakParameters.push_back(parameterNames[index]);
        }
    }

    for (std::size_t left = 0; left + 1 < parameterNames.size(); ++left) {
        if (columnNorms[left] < options.minimumSensitivity) {
            continue;
        }
        for (std::size_t right = left + 1; right < parameterNames.size(); ++right) {
            if (columnNorms[right] < options.minimumSensitivity) {
                continue;
            }
            const auto dot = dotColumns(sensitivity, left, right);
            const auto correlation = dot / (columnNorms[left] * columnNorms[right]);
            if (std::fabs(correlation) >= options.strongCorrelationThreshold) {
                ParameterCorrelation entry {};
                entry.first = parameterNames[left];
                entry.second = parameterNames[right];
                entry.correlation = correlation;
                result.strongCorrelations.push_back(entry);
            }
        }
    }

    const auto svd = ss::jacobiSingularValues(sensitivity);
    result.singularValues = svd.singularValues;
    if (svd.singularValues.empty()) {
        result.conditionNumber = std::numeric_limits<double>::infinity();
    } else {
        const auto sigmaMax = svd.singularValues.front();
        const auto sigmaMin = svd.singularValues.back();
        if (!(sigmaMin > 0.0) || sigmaMin < 1.0e-300) {
            result.conditionNumber = std::numeric_limits<double>::infinity();
        } else {
            result.conditionNumber = sigmaMax / sigmaMin;
        }
    }

    if (!svd.converged) {
        result.failures.push_back("Jacobi SVD did not converge within the sweep budget");
    }
    if (result.conditionNumber > options.conditionNumberMax) {
        result.failures.push_back("condition number exceeds limit");
    }
    if (!result.weakParameters.empty()) {
        result.failures.push_back("at least one parameter falls below the sensitivity floor");
    }
    if (!result.strongCorrelations.empty()) {
        result.failures.push_back("at least one parameter pair is strongly correlated");
    }
    if (!result.parametersOnBound.empty()) {
        result.failures.push_back("at least one parameter sits on a bound");
    }

    result.passed = result.failures.empty()
        && std::isfinite(result.conditionNumber)
        && result.conditionNumber <= options.conditionNumberMax;
    return result;
}

[[nodiscard]] ss::CircuitGraph applyParametersCopy(ss::CircuitGraph circuit,
                                                   const ActiveModelParameters& parameters)
{
    const auto saturationCurrent = std::exp(parameters.logIs.value);
    for (auto& diode : circuit.mutableDiodes()) {
        diode.model.saturationCurrentAmp = saturationCurrent;
        diode.model.gminSiemens = parameters.numericalGmin.value;
    }
    for (auto& bjt : circuit.mutableNpnBjts()) {
        bjt.model.saturationCurrentAmp = saturationCurrent;
        bjt.model.betaForward = parameters.betaForward.value;
        bjt.model.betaReverse = parameters.betaReverse.value;
        bjt.model.gminSiemens = parameters.numericalGmin.value;
    }
    return circuit;
}

} // namespace

IdentifiabilityResult IdentifiabilityAnalyzer::analyze(
    const BoundedCalibrationProblem& problem,
    const ActiveModelParameters& parameters,
    const BoundedCalibrationOptions& calibrationOptions,
    const IdentifiabilityOptions& options) const
{
    IdentifiabilityResult result {};
    result.validInput = parameters.isValid() && problem.dataset.isValid();
    if (!result.validInput) {
        result.failures.push_back("invalid parameters or dataset for identifiability");
        return result;
    }

    const BoundedCalibrationSolver solver {};
    const auto names = activeModelParameterNames();
    const auto centerEvaluation = solver.evaluate(problem,
                                                  parameters,
                                                  problem.trainingDcScenarios,
                                                  calibrationOptions);
    if (!centerEvaluation.validInput) {
        for (const auto& failure : centerEvaluation.failures) {
            result.failures.push_back("center evaluate: " + failure);
        }
        return result;
    }
    const auto residualCount = centerEvaluation.normalizedResiduals.size();
    if (residualCount == 0) {
        result.failures.push_back("center evaluation produced no residuals");
        return result;
    }

    std::vector<std::string> parametersOnBound {};
    ss::DenseMatrix sensitivity {residualCount, names.size()};

    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto& bound = activeModelParameterAt(parameters, index);
        const auto span = bound.upper - bound.lower;
        const auto step = options.perturbationRelative * (span > 0.0 ? span : 1.0);
        if (step <= 0.0) {
            continue;
        }

        auto plusParams = parameters;
        auto minusParams = parameters;
        const auto upperHeadroom = bound.upper - bound.value;
        const auto lowerHeadroom = bound.value - bound.lower;
        bool onBound = false;

        if (upperHeadroom < step * 0.5) {
            // Unilateral backward difference at the upper bound.
            setActiveModelParameterValue(minusParams, index, bound.value - step);
            const auto plusEvaluation = centerEvaluation;
            const auto minusEvaluation = solver.evaluate(problem,
                                                         minusParams,
                                                         problem.trainingDcScenarios,
                                                         calibrationOptions);
            if (!minusEvaluation.validInput) {
                continue;
            }
            const auto rows = std::min(residualCount, minusEvaluation.normalizedResiduals.size());
            const auto effectiveStep = bound.value
                - activeModelParameterAt(minusParams, index).value;
            if (effectiveStep <= 0.0) {
                continue;
            }
            for (std::size_t row = 0; row < rows; ++row) {
                const auto derivative = (plusEvaluation.normalizedResiduals[row]
                    - minusEvaluation.normalizedResiduals[row]) / effectiveStep;
                sensitivity.at(row, index) = std::isfinite(derivative) ? derivative : 0.0;
            }
            onBound = true;
        } else if (lowerHeadroom < step * 0.5) {
            // Unilateral forward difference at the lower bound.
            setActiveModelParameterValue(plusParams, index, bound.value + step);
            const auto minusEvaluation = centerEvaluation;
            const auto plusEvaluation = solver.evaluate(problem,
                                                        plusParams,
                                                        problem.trainingDcScenarios,
                                                        calibrationOptions);
            if (!plusEvaluation.validInput) {
                continue;
            }
            const auto rows = std::min(residualCount, plusEvaluation.normalizedResiduals.size());
            const auto effectiveStep = activeModelParameterAt(plusParams, index).value
                - bound.value;
            if (effectiveStep <= 0.0) {
                continue;
            }
            for (std::size_t row = 0; row < rows; ++row) {
                const auto derivative = (plusEvaluation.normalizedResiduals[row]
                    - minusEvaluation.normalizedResiduals[row]) / effectiveStep;
                sensitivity.at(row, index) = std::isfinite(derivative) ? derivative : 0.0;
            }
            onBound = true;
        } else {
            setActiveModelParameterValue(plusParams, index, bound.value + step);
            setActiveModelParameterValue(minusParams, index, bound.value - step);
            const auto plusEvaluation = solver.evaluate(problem,
                                                        plusParams,
                                                        problem.trainingDcScenarios,
                                                        calibrationOptions);
            const auto minusEvaluation = solver.evaluate(problem,
                                                         minusParams,
                                                         problem.trainingDcScenarios,
                                                         calibrationOptions);
            if (!plusEvaluation.validInput || !minusEvaluation.validInput) {
                continue;
            }
            const auto rows = std::min(residualCount,
                                       std::min(plusEvaluation.normalizedResiduals.size(),
                                                minusEvaluation.normalizedResiduals.size()));
            const auto plusEffective = activeModelParameterAt(plusParams, index).value - bound.value;
            const auto minusEffective = bound.value - activeModelParameterAt(minusParams, index).value;
            const auto totalStep = plusEffective + minusEffective;
            if (totalStep <= 0.0) {
                continue;
            }
            for (std::size_t row = 0; row < rows; ++row) {
                const auto derivative = (plusEvaluation.normalizedResiduals[row]
                    - minusEvaluation.normalizedResiduals[row]) / totalStep;
                sensitivity.at(row, index) = std::isfinite(derivative) ? derivative : 0.0;
            }
        }

        if (onBound) {
            parametersOnBound.push_back(names[index]);
        }
    }

    // Apply the parameters once so that residuals remain self-consistent with
    // the center evaluation (defensive: the helper does not retain state).
    (void) applyParametersCopy(problem.referenceCircuit, parameters);

    return evaluateFromSensitivity(names, sensitivity, parametersOnBound, options);
}

IdentifiabilityResult IdentifiabilityAnalyzer::analyzeSensitivityMatrix(
    const std::vector<std::string>& parameterNames,
    const ss::DenseMatrix& sensitivity,
    const std::vector<std::string>& parametersOnBound,
    const IdentifiabilityOptions& options) const
{
    return evaluateFromSensitivity(parameterNames, sensitivity, parametersOnBound, options);
}

} // namespace u273::reference::calibration
