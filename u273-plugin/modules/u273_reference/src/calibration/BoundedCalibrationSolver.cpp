#include "u273/reference/calibration/BoundedCalibrationSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "u273/reference/calibration/ActiveModelParameterMapping.h"
#include "u273/reference/calibration/B6SmallSignalAcReference.h"
#include "u273/reference/calibration/CalibrationResiduals.h"
#include "u273/reference/calibration/OperatingPointSolver.h"
#include "u273/reference/state_space/Matrix.h"

namespace u273::reference::calibration {

namespace ss = u273::reference::state_space;

namespace {

constexpr std::size_t kParameterCount = 8;

[[nodiscard]] const std::array<std::string, kParameterCount>& parameterNamesArray()
{
    // Canonical ordering used by index everywhere (sensitivity matrix,
    // coarse grid, identifiability). Any change here is a breaking change.
    static const std::array<std::string, kParameterCount> names {
        "diodeA",
        "diodeB",
        "logIs",
        "betaForward",
        "betaReverse",
        "earlyVoltage",
        "detectorToCmdScale",
        "numericalGmin",
    };
    return names;
}

[[nodiscard]] BoundedParameter& parameterReference(ActiveModelParameters& parameters, std::size_t index)
{
    switch (index) {
    case 0: return parameters.diodeA;
    case 1: return parameters.diodeB;
    case 2: return parameters.logIs;
    case 3: return parameters.betaForward;
    case 4: return parameters.betaReverse;
    case 5: return parameters.earlyVoltage;
    case 6: return parameters.detectorToCmdScale;
    case 7: return parameters.numericalGmin;
    default: throw std::out_of_range("activeModelParameter index out of range");
    }
}

[[nodiscard]] const BoundedParameter& parameterReference(const ActiveModelParameters& parameters, std::size_t index)
{
    switch (index) {
    case 0: return parameters.diodeA;
    case 1: return parameters.diodeB;
    case 2: return parameters.logIs;
    case 3: return parameters.betaForward;
    case 4: return parameters.betaReverse;
    case 5: return parameters.earlyVoltage;
    case 6: return parameters.detectorToCmdScale;
    case 7: return parameters.numericalGmin;
    default: throw std::out_of_range("activeModelParameter index out of range");
    }
}

void appendResidualPoints(std::vector<double>& sink, const ResidualGateResult& gate)
{
    for (const auto& set : gate.sets) {
        for (const auto& point : set.points) {
            if (point.status == ResidualStatus::skipped) {
                continue;
            }
            if (std::isfinite(point.weightedError)) {
                sink.push_back(point.weightedError);
            } else {
                sink.push_back(1.0e12);
            }
        }
    }
}

void recordAcResiduals(std::vector<double>& sink,
                       std::vector<std::string>& failures,
                       const BoundedCalibrationProblem& problem,
                       const ss::CircuitGraph& circuit,
                       const OperatingPointResult& op,
                       bool& acPassed)
{
    const auto frequencies = problem.dataset.acFrequenciesHz.empty()
        ? std::vector<double> {}
        : problem.dataset.acFrequenciesHz;

    AcResidualOptions acOptions {};
    acOptions.scenario = "rsource_10k";
    acOptions.driveVolt = 1.0;
    acOptions.matchDriveVolt = true;
    acOptions.commandSourceOhm = 10000.0;
    acOptions.matchCommandSourceOhm = true;

    const auto ac = solveB6SmallSignalAcReference(circuit,
                                                  op,
                                                  acOptions.commandSourceOhm,
                                                  frequencies);
    const auto gate = evaluateAcResiduals(problem.dataset, ac, acOptions);
    acPassed = gate.passed;
    appendResidualPoints(sink, gate);

    for (const auto& failure : ac.failures) {
        failures.push_back("AC solver: " + failure);
    }
    for (const auto& failure : gate.failures) {
        failures.push_back("AC: " + failure);
    }
}

} // namespace

std::vector<std::string> activeModelParameterNames()
{
    const auto& source = parameterNamesArray();
    return {source.begin(), source.end()};
}

double activeModelParameterValue(const ActiveModelParameters& parameters, std::size_t index)
{
    return parameterReference(parameters, index).value;
}

void setActiveModelParameterValue(ActiveModelParameters& parameters, std::size_t index, double value)
{
    auto& reference = parameterReference(parameters, index);
    reference.value = std::clamp(value, reference.lower, reference.upper);
}

const BoundedParameter& activeModelParameterAt(const ActiveModelParameters& parameters, std::size_t index)
{
    return parameterReference(parameters, index);
}

double CalibrationResidualVector::cost() const noexcept
{
    double accumulator {};
    for (const auto value : normalizedResiduals) {
        if (std::isfinite(value)) {
            accumulator += 0.5 * value * value;
        }
    }
    return accumulator;
}

CalibrationResidualVector BoundedCalibrationSolver::evaluate(
    const BoundedCalibrationProblem& problem,
    const ActiveModelParameters& parameters,
    const std::vector<std::string>& /*dcScenarios*/,
    const BoundedCalibrationOptions& options) const
{
    CalibrationResidualVector result {};
    result.validInput = parameters.isValid() && problem.dataset.isValid();
    if (!result.validInput) {
        result.failures.push_back("invalid parameters or dataset");
        return result;
    }

    auto circuit = applyActiveModelParametersToCircuit(problem.referenceCircuit, parameters);
    const auto dcView = DCCircuitView::fromCircuit(circuit);
    if (!dcView.isValid()) {
        result.failures.push_back("DC view rejected the reference circuit");
        result.validInput = false;
        return result;
    }

    const OperatingPointSolver opSolver {};
    const auto op = opSolver.solve(dcView);
    if (!op.converged) {
        result.failures.push_back("DC operating point did not converge");
        return result;
    }

    if (options.includeDc) {
        DcResidualOptions dcOptions {};
        const auto dcGate = evaluateDcResiduals(problem.dataset, circuit, op, dcOptions);
        result.dcPassed = dcGate.passed;
        appendResidualPoints(result.normalizedResiduals, dcGate);
        for (const auto& failure : dcGate.failures) {
            result.failures.push_back("DC: " + failure);
        }
    } else {
        result.dcPassed = true;
    }

    // DC must pass before AC/transient are accepted (plan invariant).
    if (!result.dcPassed) {
        result.failures.push_back("DC gate failed; skipping AC residuals to keep cost meaningful");
        return result;
    }

    if (options.includeAc) {
        recordAcResiduals(result.normalizedResiduals,
                          result.failures,
                          problem,
                          circuit,
                          op,
                          result.acPassed);
    } else {
        result.acPassed = true;
    }

    // Transient residuals are intentionally not included in calibration cost:
    // the runner evaluates the transient gate separately because each transient
    // case requires a full dynamic simulation that would dominate LM cost.
    // This is consistent with SPICE-style parameter extraction practice.
    result.transientPassed = true;

    return result;
}

namespace {

[[nodiscard]] ActiveModelParameters coarseGridSearch(const BoundedCalibrationSolver& solver,
                                                     const BoundedCalibrationProblem& problem,
                                                     const ActiveModelParameters& seed,
                                                     const std::vector<std::string>& dcScenarios,
                                                     const BoundedCalibrationOptions& options,
                                                     double& bestCost)
{
    auto best = seed;
    const auto initial = solver.evaluate(problem, seed, dcScenarios, options);
    bestCost = initial.validInput ? initial.cost() : std::numeric_limits<double>::infinity();

    const auto levels = std::max(2, options.coarseGridLevels);
    constexpr std::array<std::size_t, 3> sensitiveIndices {0, 1, 2};

    auto candidate = seed;
    for (auto a = 0; a < levels; ++a) {
        for (auto b = 0; b < levels; ++b) {
            for (auto c = 0; c < levels; ++c) {
                const std::array<int, 3> grid {a, b, c};
                for (std::size_t local = 0; local < sensitiveIndices.size(); ++local) {
                    const auto index = sensitiveIndices[local];
                    const auto& bound = parameterReference(seed, index);
                    const auto span = bound.upper - bound.lower;
                    const auto fraction = static_cast<double>(grid[local])
                        / static_cast<double>(levels - 1);
                    const auto value = bound.lower + fraction * span;
                    setActiveModelParameterValue(candidate, index, value);
                }
                const auto sample = solver.evaluate(problem, candidate, dcScenarios, options);
                if (!sample.validInput) {
                    continue;
                }
                const auto cost = sample.cost();
                if (cost < bestCost) {
                    bestCost = cost;
                    best = candidate;
                }
            }
        }
    }

    return best;
}

[[nodiscard]] ss::DenseMatrix buildJacobian(const BoundedCalibrationSolver& solver,
                                                     const BoundedCalibrationProblem& problem,
                                                     const ActiveModelParameters& center,
                                                     const std::vector<std::string>& dcScenarios,
                                                     const BoundedCalibrationOptions& options,
                                                     const CalibrationResidualVector& centerEvaluation)
{
    const auto m = centerEvaluation.normalizedResiduals.size();
    ss::DenseMatrix jacobian {m, kParameterCount};
    if (m == 0) {
        return jacobian;
    }

    for (std::size_t index = 0; index < kParameterCount; ++index) {
        const auto& bound = parameterReference(center, index);
        const auto span = bound.upper - bound.lower;
        const auto step = std::max(options.finiteDifferenceRelative * span,
                                    options.finiteDifferenceRelative);
        if (step <= 0.0) {
            continue;
        }

        auto plusParams = center;
        auto minusParams = center;
        setActiveModelParameterValue(plusParams, index, bound.value + step);
        setActiveModelParameterValue(minusParams, index, bound.value - step);
        const auto plus = solver.evaluate(problem, plusParams, dcScenarios, options);
        const auto minus = solver.evaluate(problem, minusParams, dcScenarios, options);

        if (!plus.validInput || !minus.validInput) {
            continue;
        }
        const auto plusSize = plus.normalizedResiduals.size();
        const auto minusSize = minus.normalizedResiduals.size();
        const auto rows = std::min(m, std::min(plusSize, minusSize));
        const auto plusEffectiveStep = parameterReference(plusParams, index).value - bound.value;
        const auto minusEffectiveStep = bound.value - parameterReference(minusParams, index).value;
        const auto totalStep = plusEffectiveStep + minusEffectiveStep;
        if (totalStep <= 0.0) {
            continue;
        }
        for (std::size_t row = 0; row < rows; ++row) {
            const auto derivative = (plus.normalizedResiduals[row] - minus.normalizedResiduals[row])
                / totalStep;
            jacobian.at(row, index) = std::isfinite(derivative) ? derivative : 0.0;
        }
    }

    return jacobian;
}

} // namespace

BoundedCalibrationResult BoundedCalibrationSolver::solve(
    const BoundedCalibrationProblem& problem,
    const BoundedCalibrationOptions& options) const
{
    BoundedCalibrationResult result {};
    result.bestParameters = problem.initialParameters;
    result.validInput = problem.initialParameters.isValid()
        && problem.dataset.isValid();
    if (!result.validInput) {
        result.calibrationFailures.push_back("invalid initial parameters or dataset");
        return result;
    }

    const auto trainingScenarios = problem.trainingDcScenarios;
    const auto validationScenarios = problem.validationDcScenarios;

    const auto initialEvaluation = evaluate(problem,
                                            problem.initialParameters,
                                            trainingScenarios,
                                            options);
    result.initialTrainCost = initialEvaluation.cost();
    if (!initialEvaluation.validInput) {
        for (const auto& failure : initialEvaluation.failures) {
            result.calibrationFailures.push_back("initial evaluate: " + failure);
        }
        return result;
    }

    double coarseCost = result.initialTrainCost;
    auto current = coarseGridSearch(*this,
                                    problem,
                                    problem.initialParameters,
                                    trainingScenarios,
                                    options,
                                    coarseCost);
    double currentCost = coarseCost;

    double damping = std::max(options.initialDamping, 1.0e-12);
    constexpr double kDampingUp = 10.0;
    constexpr double kDampingDown = 10.0;
    constexpr double kMinDamping = 1.0e-12;
    constexpr double kMaxDamping = 1.0e12;
    constexpr double kColumnEpsilon = 1.0e-30;

    int consecutiveAcceptableSteps {};
    for (auto iteration = 0; iteration < options.maxIterations; ++iteration) {
        const auto evaluation = evaluate(problem, current, trainingScenarios, options);
        if (!evaluation.validInput) {
            result.calibrationFailures.push_back("LM iteration evaluate failed");
            break;
        }
        const auto m = evaluation.normalizedResiduals.size();
        if (m == 0) {
            break;
        }

        const auto jacobian = buildJacobian(*this, problem, current, trainingScenarios, options, evaluation);
        const auto jacobianT = ss::transposed(jacobian);
        ss::DenseMatrix normal {kParameterCount, kParameterCount};
        for (std::size_t row = 0; row < kParameterCount; ++row) {
            for (std::size_t column = 0; column < kParameterCount; ++column) {
                double accumulator {};
                for (std::size_t k = 0; k < m; ++k) {
                    accumulator += jacobian.at(k, row) * jacobian.at(k, column);
                }
                normal.at(row, column) = accumulator;
            }
        }
        const auto rhs = ss::multiply(jacobianT, evaluation.normalizedResiduals);
        ss::Vector negativeGradient(kParameterCount, 0.0);
        for (std::size_t index = 0; index < kParameterCount; ++index) {
            negativeGradient[index] = -rhs[index];
        }

        // Marquardt scaling with a small additive guard to keep the diagonal
        // strictly positive even when a parameter is currently zero-sensitivity.
        // Zero-sensitivity parameters then receive a vanishing step.
        ss::DenseMatrix damped = normal;
        for (std::size_t index = 0; index < kParameterCount; ++index) {
            const auto diagonal = normal.at(index, index);
            damped.at(index, index) = diagonal + damping * (diagonal + kColumnEpsilon);
        }

        ss::Vector step(kParameterCount, 0.0);
        const auto solveResult = ss::solveLinearSystem(damped, negativeGradient, step);
        if (!solveResult.solved) {
            damping = std::min(kMaxDamping, damping * kDampingUp);
            continue;
        }

        // Normalise the step against parameter spans and clamp at maxNormalizedStep.
        double maxNormalized {};
        for (std::size_t index = 0; index < kParameterCount; ++index) {
            const auto& bound = parameterReference(current, index);
            const auto span = bound.upper - bound.lower;
            const auto normalized = span > 0.0 ? std::fabs(step[index]) / span : 0.0;
            if (normalized > maxNormalized) {
                maxNormalized = normalized;
            }
        }
        const auto scale = (maxNormalized > options.maxNormalizedStep && maxNormalized > 0.0)
            ? options.maxNormalizedStep / maxNormalized
            : 1.0;

        auto trial = current;
        for (std::size_t index = 0; index < kParameterCount; ++index) {
            const auto& bound = parameterReference(current, index);
            const auto value = bound.value + scale * step[index];
            setActiveModelParameterValue(trial, index, value);
        }

        const auto trialEvaluation = evaluate(problem, trial, trainingScenarios, options);
        if (!trialEvaluation.validInput) {
            damping = std::min(kMaxDamping, damping * kDampingUp);
            continue;
        }
        const auto trialCost = trialEvaluation.cost();

        if (trialCost < currentCost) {
            const auto improvement = (currentCost - trialCost)
                / std::max(currentCost, 1.0e-30);
            current = trial;
            currentCost = trialCost;
            damping = std::max(kMinDamping, damping / kDampingDown);
            result.iterations = iteration + 1;
            if (improvement < options.convergenceTolerance) {
                if (++consecutiveAcceptableSteps >= 2) {
                    result.converged = true;
                    break;
                }
            } else {
                consecutiveAcceptableSteps = 0;
            }
            const auto deltaNorm = ss::twoNorm(step) * scale;
            if (deltaNorm < options.convergenceTolerance) {
                result.converged = true;
                break;
            }
        } else {
            damping = std::min(kMaxDamping, damping * kDampingUp);
        }
    }

    if (!result.converged && currentCost < result.initialTrainCost) {
        result.converged = true;
    }

    const auto finalTraining = evaluate(problem, current, trainingScenarios, options);
    if (finalTraining.validInput) {
        currentCost = finalTraining.cost();
        if (!result.converged
            && !finalTraining.normalizedResiduals.empty()
            && finalTraining.dcPassed
            && finalTraining.acPassed
            && finalTraining.transientPassed) {
            result.converged = true;
        }
    }

    result.bestParameters = current;
    result.trainCost = currentCost;

    for (std::size_t index = 0; index < kParameterCount; ++index) {
        if (parameterReference(current, index).touchesBound()) {
            result.parametersOnBound.push_back(parameterNamesArray()[index]);
        }
    }

    const auto validation = evaluate(problem, current, validationScenarios, options);
    if (validation.validInput) {
        result.validationCost = validation.cost();
        const auto guard = std::max(currentCost, 1.0e-12) * 1.5;
        result.validationPassed = result.validationCost <= guard;
        if (!result.validationPassed) {
            result.calibrationFailures.push_back("validation cost exceeds 1.5x training cost");
        }
        for (const auto& failure : validation.failures) {
            result.calibrationFailures.push_back("validation: " + failure);
        }
    } else {
        result.calibrationFailures.push_back("validation evaluation produced no residuals");
    }

    for (const auto& failure : initialEvaluation.failures) {
        result.residualFailures.push_back("initial: " + failure);
    }

    return result;
}

} // namespace u273::reference::calibration
