option(USE_GTEST "Build Pacer with gtest enabled?" OFF)
option(BUILD_REGRESSION_TESTS "Build Pacer Regression Tests for debugging?" ON)
IF(BUILD_REGRESSION_TESTS)
  add_subdirectory( RegressionTests )
ENDIF(BUILD_REGRESSION_TESTS)

option(BUILD_UNIT_TESTS "Build Pacer Unit tests for debugging?" ON)
IF(BUILD_UNIT_TESTS)
  add_subdirectory( UnitTests )
ENDIF(BUILD_UNIT_TESTS)

option(BUILD_DEMOS "Build Pacer Demos for demonstration/debugging?" ON)
IF(BUILD_DEMOS)
  add_subdirectory( Demos )
ENDIF(BUILD_DEMOS)
