# Build a static version of openssl to link into
set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/openssl-install")
add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
add_dependencies(OpenSSL::Crypto PUBLIC openssl-build)
add_dependencies(OpenSSL::SSL PUBLIC openssl-build)
set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_INSTALL_DIR}/lib64/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
)
set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_INSTALL_DIR}/lib64/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
)
get_target_property(libcryptoa OpenSSL::Crypto IMPORTED_LOCATION)
get_target_property(libcryptoaloc OpenSSL::Crypto LOCATION)
set(OPENSSL_C_FLAGS "-O3 -march=x86-64-v3" CACHE STRING "OpenSSL custom CFLAGS" FORCE)
set(OPENSSL_CXX_FLAGS "-O3 -march=x86-64-v3" CACHE STRING "OpenSSL custom CXXFLAGS" FORCE)
add_custom_command(
        OUTPUT ${OPENSSL_INSTALL_DIR}/lib64/libcrypto.a ${OPENSSL_INSTALL_DIR}/lib64/libssl.a
        COMMAND ${FETCHCONTENT_BASE_DIR}/openssl-source-src/Configure CFLAGS=${OPENSSL_C_FLAGS} CXXFLAGS=${OPENSSL_CXX_FLAGS} no-shared no-tests --prefix=${OPENSSL_INSTALL_DIR} --openssldir=${OPENSSL_INSTALL_DIR} linux-x86_64
        COMMAND make -j
        COMMAND make install_sw # only installs software components (no docs, etc)
        COMMENT "Build openssl as a static library"
        WORKING_DIRECTORY ${FETCHCONTENT_BASE_DIR}/openssl-source-build
)
add_custom_target(openssl-build ALL
        DEPENDS ${OPENSSL_INSTALL_DIR}/lib64/libcrypto.a ${OPENSSL_INSTALL_DIR}/lib64/libssl.a
)

add_library(mustache INTERFACE)
target_include_directories(mustache INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/kainjow)
add_library(mustache::mustache ALIAS mustache)

FetchContent_Declare(
        zeromq
        GIT_REPOSITORY https://github.com/zeromq/libzmq.git
        GIT_TAG v4.3.4 # latest v4.3.4
)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "Build the tests for ZeroMQ")
# suppress warnings for missing zeromq dependencies by disabling some features
set(WITH_TLS OFF CACHE BOOL "TLS support for ZeroMQ WebSockets")
set(BUILD_SHARED OFF CACHE BOOL "Build cmake shared library")
option(WITH_PERF_TOOL "Build with perf-tools" OFF)

FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.19.0
)

# zlib: optional httplib dependency
FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.2.12 # latest v1.2.12
)

FetchContent_Declare(
        openssl-source
        GIT_REPOSITORY https://github.com/openssl/openssl.git
        GIT_TAG openssl-3.4.1
)

FetchContent_MakeAvailable(cpp-httplib zeromq openssl-source)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x
