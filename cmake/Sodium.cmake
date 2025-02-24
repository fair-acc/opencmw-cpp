# libsodium does not have cmake support, so we need to add it ourself
FetchContent_Declare(
        libsodium
        GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
        GIT_TAG 1.0.20-RELEASE
)

FetchContent_MakeAvailable(libsodium)

file(GLOB_RECURSE SODIUM_C "${libsodium_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE SODIUM_H "${libsodium_SOURCE_DIR}/src/*.h")

add_library(sodium STATIC ${SODIUM_C} ${SODIUM_H})

target_include_directories(sodium PUBLIC $<BUILD_INTERFACE:${libsodium_SOURCE_DIR}/src/libsodium/include> $<INSTALL_INTERFACE:include/opencmw> PRIVATE "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium")
# silence warnings about being built by a different build system
target_compile_definitions(sodium PRIVATE CONFIGURED)

# allow zeromq to also use this version of sodium instead of using it's internal conflicting tweetnacl
set(SODIUM_FOUND TRUE)
set(SODIUM_INCLUDE_DIRS
    "$<BUILD_INTERFACE:${libsodium_SOURCE_DIR}/src/libsodium/include>"
    "$<INSTALL_INTERFACE:include/opencmw>"
    "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium"
)
set(SODIUM_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/libsodium.a")
set(SODIUM_LIBRARIES "${CMAKE_CURRENT_BINARY_DIR}/libsodium.a")
set(SODIUM_VERSION "28.0")

set(SODIUM_LIBRARY_VERSION_MAJOR 28)
set(SODIUM_LIBRARY_VERSION_MINOR 0)
configure_file("${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h.in" "${libsodium_SOURCE_DIR}/src/libsodium/include/sodium/version.h")

install(TARGETS sodium EXPORT opencmwTargets PUBLIC_HEADER DESTINATION include/opencmw)
