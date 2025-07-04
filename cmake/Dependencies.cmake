include(FetchContent)

# cmake project, fetchContent supported
FetchContent_Declare(
        refl-cpp
        GIT_REPOSITORY https://github.com/veselink1/refl-cpp.git
        GIT_TAG 27fbd7d2e6d86bc135b87beef6b5f7ce53afd4fc # v0.12.3+4 11/2022
)

# fetch content support
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v2.13.9 # latest: v2.13.9 or v3.1.0
)

# dependency of mp-units, building examples, tests, etc is off by default
FetchContent_Declare(
        gsl-lite
        GIT_REPOSITORY https://github.com/gsl-lite/gsl-lite.git
        GIT_TAG v0.42.0 # latest as of 2025-04-02
)

FetchContent_MakeAvailable(gsl-lite)

set(gsl-lite_DIR ${gsl-lite_BINARY_DIR})

set(mp_units_patch_command git apply --ignore-space-change --ignore-whitespace "${CMAKE_CURRENT_SOURCE_DIR}/patches/mp-units-disable-broken-ctad.diff")
set(UNITS_USE_LIBFMT OFF)
FetchContent_Declare(
        mp-units
        GIT_REPOSITORY https://github.com/mpusz/units.git
        GIT_TAG 59a1269174da961fd21fa2bea6c1b964e80ce2e3 # v0.8.0
        PATCH_COMMAND ${mp_units_patch_command} || true
        SOURCE_SUBDIR src/
)

FetchContent_MakeAvailable(refl-cpp mp-units catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x

include(cmake/Sodium.cmake)
