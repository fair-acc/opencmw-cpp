cmake_minimum_required(VERSION 3.19)
set(CMAKE_CXX_STANDARD 20)

project(opencmw-cpp CXX)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(GNUInstallDirs)

include(cmake/CMakeRC.cmake)

# Check for supported compiler versions
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.1.0)
        message(FATAL_ERROR "GCC>=11.1.0 required, but gcc ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13.0.0)
        message(FATAL_ERROR "Clang>=13.0.0 required, but clang ${CMAKE_CXX_COMPILER_VERSION} detected.")
    endif ()
else ()
    message(WARN "No version check for your compiler (${CMAKE_CXX_COMPILER_ID}) implemented, "
            "in case of build problems consider updating your compiler or check if you can switch to gcc or clang")
endif ()

# Link this 'library' to set the c++ standard / compile-time options requested
add_library(project_options INTERFACE)
target_include_directories(project_options INTERFACE ${CMAKE_SOURCE_DIR}/3rd_party)

target_compile_features(project_options INTERFACE cxx_std_20)

if (CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    option(ENABLE_BUILD_WITH_TIME_TRACE "Enable -ftime-trace to generate time tracing .json files on clang" OFF)
    if (ENABLE_BUILD_WITH_TIME_TRACE)
        target_compile_options(project_options INTERFACE -ftime-trace)
    endif ()
endif ()

# enable cache system
include(cmake/Cache.cmake)

# Link this 'library' to use the warnings specified in CompilerWarnings.cmake
add_library(project_warnings INTERFACE)
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

option(ENABLE_TESTING "Enable Test Builds" ON)
if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR GENERATOR_IS_MULTI_CONFIG)
    option(ENABLE_COVERAGE "Enable Coverage" ON)
else ()
    option(ENABLE_COVERAGE "Enable Coverage" OFF)
endif ()
option(ENABLE_CONCEPTS "Enable Concepts Builds" ON)

# Very basic PCH example
option(ENABLE_PCH "Enable Precompiled Headers" OFF)
if (ENABLE_PCH)
    # This sets a global PCH parameter, each project will build its own PCH, which is a good idea if any #define's change
    #
    # consider breaking this out per project as necessary
    target_precompile_headers(project_options INTERFACE
            <vector>
            <string>
            <map>
            <utility>)
endif ()

include(cmake/Conan.cmake)
run_conan()
include(cmake/ReflCpp.cmake) # todo: switch to obtaining reflcpp from conan

if (ENABLE_TESTING)
    enable_testing()
    message("Building Tests.")
    if (ENABLE_COVERAGE)
        if (UNIX AND NOT APPLE) # Linux
            message("Coverage reporting enabled")
            include(cmake/CodeCoverage.cmake) # https://github.com/bilke/cmake-modules/blob/master/CodeCoverage.cmake
            # (License: BSL-1.0)
            target_compile_options(project_options INTERFACE --coverage -O0 -g -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0) # fortify_source is not possible without optimization
            target_link_libraries(project_options INTERFACE --coverage)
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
