add_library(u273_reference STATIC
    src/calibration/ActiveModelParameterMapping.cpp
    src/calibration/ActiveTopologyEvaluator.cpp
    src/calibration/ActiveTopologyCandidate.cpp
    src/calibration/ActiveTopologyInventory.cpp
    src/calibration/B6B11CalibrationRunner.cpp
    src/calibration/B6SmallSignalAcReference.cpp
    src/calibration/BoundedCalibrationSolver.cpp
    src/calibration/CalibrationDataset.cpp
    src/calibration/CalibrationResiduals.cpp
    src/calibration/IdentifiabilityAnalyzer.cpp
    src/calibration/LinearizedAcSolver.cpp
    src/calibration/OperatingPointSolver.cpp
    src/calibration/ReductionBuilder.cpp
    src/calibration/SonicTarget.cpp
    src/calibration/ThdBench.cpp
    src/calibration/TransientReferenceSolver.cpp
    src/GoldenValidationSuite.cpp
    src/ReferenceValidator.cpp
    src/ScientificReferenceModel.cpp
    src/state_space/CircuitGraph.cpp
    src/state_space/ComponentModels.cpp
    src/state_space/Matrix.cpp
    src/state_space/NewtonSolver.cpp
    src/state_space/StateSpaceSolver.cpp
    src/state_space/U273NetlistLoader.cpp
    src/state_space/U273ReferenceCircuitBuilder.cpp)

add_library(u273::reference ALIAS u273_reference)

target_include_directories(u273_reference PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_link_libraries(u273_reference PUBLIC u273::core u273::assets)

u273_target_defaults(u273_reference)
