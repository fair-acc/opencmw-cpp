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
        GIT_TAG v4.3.5 # latest as of 2025-03-27
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
        GIT_TAG openssl-3.5.0 # 3.5.0 required for server-side QUIC support
)

FetchContent_MakeAvailable(cpp-httplib zeromq openssl-source)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x

option(ENABLE_NGHTTP2_DEBUG "Enable verbose nghttp2 debug output" OFF)

include(ExternalProject)
ExternalProject_Add(Nghttp2Project
        GIT_REPOSITORY https://github.com/nghttp2/nghttp2
        GIT_TAG v1.65.0
        GIT_SHALLOW ON
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/nghttp2-install/lib/libnghttp2.a
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/nghttp2-install
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DENABLE_HTTP3:BOOL=OFF
        -DENABLE_DEBUG:BOOL=${ENABLE_NGHTTP2_DEBUG}
        -DBUILD_STATIC_LIBS:BOOL=ON
        -BUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_DOC:BOOL=OFF
)

add_library(nghttp2-static STATIC IMPORTED STATIC GLOBAL)
set_target_properties(nghttp2-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/nghttp2-install/lib/libnghttp2.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/nghttp2-install/include"
)
add_dependencies(nghttp2-static Nghttp2Project)

ExternalProject_Add(Nghttp3Project
        GIT_REPOSITORY https://github.com/ngtcp2/nghttp3.git
        GIT_TAG v1.9.0
        GIT_SHALLOW ON
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/nghttp3-install/lib/libnghttp3.a
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/nghttp3-install
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_DOC:BOOL=OFF
)

# TODO(Frank) remove
# ExternalProject_Add(WolfSslProject
# GIT_REPOSITORY https://github.com/wolfSSL/wolfssl.git
# GIT_TAG v5.7.6-stable
# GIT_SHALLOW ON
# BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/ngtcp2-install/lib/libwolfssl.a
# CMAKE_ARGS
# -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
# -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/ngtcp2-install
# -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
# -DBUILD_SHARED_LIBS:BOOL=OFF
# -DWOLFSSL_QUIC:BOOL=ON
# -DWOLFSSL_CURL:BOOL=ON
# )
externalproject_add(NgTcp2Project
        GIT_REPOSITORY https://github.com/ngtcp2/ngtcp2.git
        GIT_TAG v1.12.0
        PREFIX ${CMAKE_BINARY_DIR}/ngtcp2-install
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/ngtcp2-install/lib/libngtcp2.a
        CMAKE_ARGS
        -DOPENSSL_ROOT_DIR:PATH=${OPENSSL_INSTALL_DIR}
        -DENABLE_OPENSSL:BOOL=ON
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/ngtcp2-install
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        DEPENDS OpenSSL::SSL OpenSSL::Crypto
)

add_library(ngtcp2-static STATIC IMPORTED STATIC GLOBAL)
set_target_properties(ngtcp2-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/ngtcp2-install/lib/libngtcp2.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/ngtcp2-install/include"
)
add_dependencies(ngtcp2-static NgTcp2Project)

add_library(nghttp3-static STATIC IMPORTED STATIC GLOBAL)
set_target_properties(nghttp3-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/nghttp3-install/lib/libnghttp3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/nghttp3-install/include"
)
add_dependencies(nghttp3-static Nghttp3Project)