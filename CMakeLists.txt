cmake_minimum_required(VERSION 3.15)

project(opencmw-cpp CXX)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE include/opencmw.hpp include/IoBuffer.hpp include/Debug.hpp include/Utils.hpp include/IoSerialiser.hpp include/IoClassSerialiser.hpp include/MultiArray.hpp concepts/serialiser/IoClassSerialiserBenchmark.cpp concepts/serialiser/IoClassSerialiserBenchmark.hpp)
target_compile_features(project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
  option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
  if(ENABLE_BUILD_WITH_TIME_TRACE)
    target_compile_options(project_options INTERFACE -ftime-trace)
  endif()
endif()

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)

# enable cache system
include(cmake/Cache.cmake)

# standard compiler warnings
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

# sanitizer options if supported by compiler
include(cmake/Sanitizers.cmake)
enable_sanitizers(project_options)

# enable doxygen
include(cmake/Doxygen.cmake)
enable_doxygen()

# allow for static analysis options
include(cmake/StaticAnalyzers.cmake)

option(BUILD_SHARED_LIBS "Enable compilation of shared libraries" OFF)
option(ENABLE_TESTING "Enable Test Builds" ON)
option(ENABLE_COVERAGE "Enable Coverage for Test Builds" OFF)
option(ENABLE_FUZZING "Enable Fuzzing Builds" OFF)
option(ENABLE_CONCEPTS "Enable Concepts Builds" ON)

# Very basic PCH example
option(ENABLE_PCH "Enable Precompiled Headers" OFF)
if(ENABLE_PCH)
  # This sets a global PCH parameter, each project will build its own PCH, which is a good idea if any #define's change
  #
  # consider breaking this out per project as necessary
  target_precompile_headers(
    project_options
    INTERFACE
    <vector>
    <string>
    <map>
    <utility>)
endif()

# Set up some extra Conan dependencies based on our needs before loading Conan
set(CONAN_EXTRA_REQUIRES "")
set(CONAN_EXTRA_OPTIONS "")

include(cmake/Conan.cmake)
run_conan()

add_subdirectory(include)

if(ENABLE_TESTING)
  enable_testing()
  message("Building Tests. Be sure to check out test/constexpr_tests for constexpr testing")
  add_subdirectory(test)
  if (ENABLE_COVERAGE)
    message("Coverage reporting enabled")
    include(cmake/CodeCoverage.cmake) # https://github.com/bilke/cmake-modules/blob/master/CodeCoverage.cmake (License: BSL 1.0)
    append_coverage_compiler_flags()
    # set(CMAKE_CXX_FLAGS "-g -O0 -Wall -fprofile-arcs -ftest-coverage")
    setup_target_for_coverage_gcovr_html(
      NAME coverage
      EXECUTABLE ctest
      # DEPENDENCIES
      # BASE_DIRECTORY "../"                           # Base directory for report (defaults to PROJECT_SOURCE_DIR)
      EXCLUDE "build_coverage/*" "test/*" "concepts/*" # Patterns to exclude (can be relative to BASE_DIRECTORY, with CMake 3.4+)
    )
  endif()
endif()

if(ENABLE_FUZZING)
  message("Building Fuzz Tests, using fuzzing sanitizer https://www.llvm.org/docs/LibFuzzer.html")
  add_subdirectory(fuzz_test)
endif()

if(ENABLE_CONCEPTS)
  message("Building Concepts")
  add_subdirectory(concepts)
endif()

add_subdirectory(src)

option(ENABLE_UNITY "Enable Unity builds of projects" OFF)
if(ENABLE_UNITY)
  # Add for any project you want to apply unity builds for
  set_target_properties(intro PROPERTIES UNITY_BUILD ON)
endif()
