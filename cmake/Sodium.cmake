# libsodium does not have cmake support, so we need to add it ourself
project("sodium" LANGUAGES C)

FetchContent_Declare(
        libsodium
        GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
        GIT_TAG 1.0.20-RELEASE
)

FetchContent_MakeAvailable(libsodium)

file(GLOB_RECURSE SODIUM_C "${libsodium_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE SODIUM_H "${libsodium_SOURCE_DIR}/src/*.h")

add_library(${PROJECT_NAME} ${SODIUM_C} ${SODIUM_H})

target_include_directories(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${libsodium_SOURCE_DIR}/src/libsodium/include/sodium> $<INSTALL_INTERFACE:include/opencmw> PRIVATE "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium")
# silence warnings about being built by a different build system
target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIGURED)

set(SODIUM_LIBRARY_VERSION_MAJOR 28)
set(SODIUM_LIBRARY_VERSION_MINOR 0)
configure_file("${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in" "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h")

install(TARGETS ${PROJECT_NAME} EXPORT opencmwTargets PUBLIC_HEADER DESTINATION include/opencmw)
