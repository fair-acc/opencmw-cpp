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

# fetch content support
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 8a21e328b8dcb62a2901c499598366a0f5f3f4a5 # magic, working version
        PATCH_COMMAND git config user.name 'Anonymous'
        COMMAND git config user.email '<>'
        COMMAND git cherry-pick 90b68783fff695d6ad26a56550272edd43c57b44
)

# dependency of mp-units, building examples, tests, etc is off by default
FetchContent_Declare(
        gsl-lite
        GIT_REPOSITORY https://github.com/gsl-lite/gsl-lite.git
        GIT_TAG v0.40.0
)

set(FMT_INSTALL True)
FetchContent_MakeAvailable(gsl-lite fmt)

set(gsl-lite_DIR ${gsl-lite_BINARY_DIR})
set(fmt_DIR ${fmt_BINARY_DIR})

FetchContent_Declare(
        mp-units
        GIT_REPOSITORY https://github.com/mpusz/units.git
        GIT_TAG 23debc34e28c1b69a06d458695bef8f3d7b9bbdb
        SOURCE_SUBDIR src/
)

FetchContent_MakeAvailable(gsl-lite fmt refl-cpp mp-units catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x
