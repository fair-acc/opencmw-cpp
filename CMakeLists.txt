cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)

project(opencmw-cpp CXX)

include(cmake/Dependencies.cmake)
if (NOT EMSCRIPTEN)
    include(cmake/DependenciesNative.cmake)
endif ()
# prefers usage via conan, but cmake should work, but doesn't find gsl-lite in targets
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(GNUInstallDirs)

include(cmake/CMakeRC.cmake)

configure_file(scripts/run_rest_tests.sh.in scripts/run_rest_tests.sh @ONLY)

set(USE_LIBCPP)

# Check for supported compiler versions
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.1.0)
        message(FATAL_ERROR "GCC>=11.1.0 required, but gcc ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
elseif (EMSCRIPTEN)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14.0.0)
        message(FATAL_ERROR "Clang>=14.0.0 required for emscripten, but clang ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
    # clang does not work with stdlib++ ranges, we have to use libc++
    # add_compile_options(-stdlib=libc++)
    # add_link_options(-stdlib=libc++ -lc++abi)
    message(WARNING "You are building with Emscripten Clang. Be advised that support is limited to working in conjunction with"
            " libc++ and certain modules. libc++ has been enabled.")
    add_compile_options(-Wno-shorten-64-to-32 -fwasm-exceptions)
    add_link_options(-fwasm-exceptions -sFETCH=1)
    set(USE_LIBCPP 1)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15.0.0)
        message(FATAL_ERROR "Clang>=15.0.0 required, but clang ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
else ()
    message(WARNING "No version check for your compiler (${CMAKE_CXX_COMPILER_ID}) implemented, "
            "in case of build problems consider updating your compiler or check if you can switch to gcc or clang")
endif ()

# Determine if OpenCMW is built as a subproject (using add_subdirectory) or if it is the master project.
set(opencmw_MASTER_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(opencmw_MASTER_PROJECT ON)
    message(STATUS "CMake version: ${CMAKE_VERSION}")
endif ()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(opencmw_project_options INTERFACE)

target_compile_features(opencmw_project_options INTERFACE cxx_std_20)

if (CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
    if (ENABLE_BUILD_WITH_TIME_TRACE)
        target_compile_options(opencmw_project_options INTERFACE -ftime-trace)
    endif ()
endif ()

# enable cache system
include(cmake/Cache.cmake)

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(opencmw_project_warnings INTERFACE)
# standard compiler warnings
include(cmake/CompilerWarnings.cmake)
set_project_warnings(opencmw_project_warnings)

# sanitizer options if supported by compiler
include(cmake/Sanitizers.cmake)
enable_sanitizers(opencmw_project_options)

# enable doxygen
include(cmake/Doxygen.cmake)
enable_doxygen()

# allow for static analysis options
include(cmake/StaticAnalyzers.cmake)

option(ENABLE_TESTING "Enable Test Builds" ${opencmw_MASTER_PROJECT})
if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR GENERATOR_IS_MULTI_CONFIG)
    option(ENABLE_COVERAGE "Enable Coverage" ON)
else ()
    option(ENABLE_COVERAGE "Enable Coverage" OFF)
endif ()
option(ENABLE_CONCEPTS "Enable Concepts Builds" ${opencmw_MASTER_PROJECT})

# Very basic PCH example
option(ENABLE_PCH "Enable Precompiled Headers" OFF)
if (ENABLE_PCH)
    # This sets a global PCH parameter, each project will build its own PCH, which is a good idea if any #define's change
    #
    # consider breaking this out per project as necessary
    target_precompile_headers(opencmw_project_options INTERFACE
            <vector>
            <string>
            <map>
            <utility>)
endif ()

if (ENABLE_TESTING)
    enable_testing()
    message("Building Tests.")
    if (ENABLE_COVERAGE)
        if (UNIX AND NOT APPLE AND NOT EMSCRIPTEN) # Linux
            message("Coverage reporting enabled")
            include(cmake/CodeCoverage.cmake) # https://github.com/bilke/cmake-modules/blob/master/CodeCoverage.cmake
            # (License: BSL-1.0)
            target_compile_options(opencmw_project_options INTERFACE --coverage -O0 -g -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0) # fortify_source is not possible without optimization
            target_link_libraries(opencmw_project_options INTERFACE --coverage)
            append_coverage_compiler_flags()
            setup_target_for_coverage_gcovr_xml(
                    NAME coverage
                    EXECUTABLE ctest
                    DEPENDENCIES core_tests majordomo_tests serialiser_tests
                    EXCLUDE "$CMAKE_BUILD_DIR/*" "concepts/.*" ".*/test/.*")
            setup_target_for_coverage_gcovr_html(
                    NAME coverage_html
                    EXECUTABLE ctest
                    DEPENDENCIES core_tests majordomo_tests serialiser_tests
                    EXCLUDE "$CMAKE_BUILD_DIR/*" "concepts/.*" ".*/test/.*")
        else ()
            message(WARNING "Coverage is only supported on linux")
        endif ()
    endif ()
endif ()

add_subdirectory(src)

if (ENABLE_CONCEPTS)
    message("Building Concepts")
    add_subdirectory(concepts)
endif ()
