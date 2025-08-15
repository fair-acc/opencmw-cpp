include(ExternalProject)
include(GNUInstallDirs)

set(OPENSSL_C_FLAGS "-O3 -march=x86-64-v3" CACHE STRING "OpenSSL custom CFLAGS" FORCE)
set(OPENSSL_CXX_FLAGS "-O3 -march=x86-64-v3" CACHE STRING "OpenSSL custom CXXFLAGS" FORCE)
set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/openssl-install")

# Build custom OpenSSL with QUIC support
ExternalProject_Add(OpenSslProject
        GIT_REPOSITORY https://github.com/openssl/openssl.git
        GIT_TAG openssl-3.5.0 # 3.5.0 required for server-side QUIC support
        GIT_SHALLOW ON
        BUILD_BYPRODUCTS ${OPENSSL_INSTALL_DIR}/lib64/libcrypto.a ${OPENSSL_INSTALL_DIR}/lib64/libssl.a
        CONFIGURE_COMMAND COMMAND ./Configure CFLAGS=${OPENSSL_C_FLAGS} CXXFLAGS=${OPENSSL_CXX_FLAGS} no-shared no-tests --prefix=${OPENSSL_INSTALL_DIR} --openssldir=${OPENSSL_INSTALL_DIR} linux-x86_64
        UPDATE_COMMAND ""
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install_sw # only installs software components (no docs, etc)
        BUILD_IN_SOURCE ON
)

add_library(openssl-crypto-static STATIC IMPORTED GLOBAL)
add_dependencies(openssl-crypto-static OpenSslProject)
set_target_properties(openssl-crypto-static PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_INSTALL_DIR}/lib64/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
)

add_library(openssl-ssl-static STATIC IMPORTED GLOBAL)
add_dependencies(openssl-ssl-static OpenSslProject)
set_target_properties(openssl-ssl-static PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_INSTALL_DIR}/lib64/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
)

option(ENABLE_NGHTTP_DEBUG "Enable verbose nghttp2 debug output" OFF)

ExternalProject_Add(Nghttp2Project
        GIT_REPOSITORY https://github.com/nghttp2/nghttp2
        GIT_TAG v1.65.0
        GIT_SHALLOW ON
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/_deps/nghttp2-install/${CMAKE_INSTALL_LIBDIR}/libnghttp2.a
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/_deps/nghttp2-install
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DENABLE_HTTP3:BOOL=OFF
        -DENABLE_DEBUG:BOOL=${ENABLE_NGHTTP_DEBUG}
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_STATIC_LIB:BOOL=ON
        -DENABLE_SHARED_LIB:BOOL=OFF
        -DENABLE_DOC:BOOL=OFF
)

add_library(nghttp2-static STATIC IMPORTED GLOBAL)
target_link_libraries(nghttp2-static INTERFACE ngtcp2-static ngtcp2-crypto-ossl-static)
set_target_properties(nghttp2-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/nghttp2-install/${CMAKE_INSTALL_LIBDIR}/libnghttp2.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/nghttp2-install/include"
)
add_dependencies(nghttp2-static Nghttp2Project)

ExternalProject_Add(Nghttp3Project
        GIT_REPOSITORY https://github.com/ngtcp2/nghttp3.git
        GIT_TAG v1.10.1
        GIT_SHALLOW ON
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/_deps/nghttp3-install/${CMAKE_INSTALL_LIBDIR}/libnghttp3.a
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/_deps/nghttp3-install
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DENABLE_DEBUG:BOOL=${ENABLE_NGHTTP_DEBUG}
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_STATIC_LIB:BOOL=ON
        -DENABLE_SHARED_LIB:BOOL=OFF
        -DENABLE_DOC:BOOL=OFF
)
add_library(nghttp3-static STATIC IMPORTED GLOBAL)
target_link_libraries(nghttp3-static INTERFACE ngtcp2-static ngtcp2-crypto-ossl-static)
set_target_properties(nghttp3-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/nghttp3-install/${CMAKE_INSTALL_LIBDIR}/libnghttp3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/nghttp3-install/include"
)
add_dependencies(nghttp3-static Nghttp3Project)

ExternalProject_Add(NgTcp2Project
        GIT_REPOSITORY https://github.com/ngtcp2/ngtcp2.git
        GIT_TAG v1.13.0
        GIT_SHALLOW ON
        PREFIX ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2.a ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2_crypto_ossl.a
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DOPENSSL_ROOT_DIR:PATH=${OPENSSL_INSTALL_DIR}
        -DENABLE_OPENSSL:BOOL=ON
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/_deps/ngtcp2-install
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DENABLE_DEBUG:BOOL=${ENABLE_NGHTTP_DEBUG}
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_STATIC_LIB:BOOL=ON
        -DENABLE_SHARED_LIB:BOOL=OFF
        DEPENDS openssl-crypto-static openssl-ssl-static
)

add_library(ngtcp2-static STATIC IMPORTED GLOBAL)
target_link_libraries(ngtcp2-static INTERFACE openssl-ssl-static openssl-crypto-static)
set_target_properties(ngtcp2-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/include"
)
add_dependencies(ngtcp2-static NgTcp2Project)

add_library(ngtcp2-crypto-ossl-static STATIC IMPORTED GLOBAL)
target_link_libraries(ngtcp2-crypto-ossl-static INTERFACE openssl-ssl-static openssl-crypto-static)
set_target_properties(ngtcp2-crypto-ossl-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2_crypto_ossl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/include"
)
add_dependencies(ngtcp2-crypto-ossl-static NgTcp2Project)

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

FetchContent_MakeAvailable(cpp-httplib zeromq)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x
