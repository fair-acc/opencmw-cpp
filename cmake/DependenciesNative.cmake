include(ExternalProject)
include(GNUInstallDirs)

find_package(OpenSSL 3.5.1 REQUIRED) # QUIC support OpenSSL
message(STATUS "Using system OpenSSL: ${OPENSSL_VERSION}")

option(ENABLE_NGHTTP_DEBUG "Enable verbose nghttp2 debug output" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)

# We can’t use the system ngtcp2/nghttp3 packages because Ubuntu doesn’t ship the ngtcp2_crypto_ossl backend library and headers.
ExternalProject_Add(NgTcp2Project
        GIT_REPOSITORY https://github.com/ngtcp2/ngtcp2.git
        GIT_TAG v1.18.0
        GIT_SHALLOW ON
        PREFIX ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2.a ${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2_crypto_ossl.a
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DENABLE_OPENSSL:BOOL=ON
        -DOPENSSL_USE_STATIC_LIBS:BOOL=OFF
        -DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}
        -DOPENSSL_INCLUDE_DIR:PATH=${OPENSSL_INCLUDE_DIR}
        -DOPENSSL_SSL_LIBRARY:FILEPATH=${OPENSSL_SSL_LIBRARY}
        -DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${OPENSSL_CRYPTO_LIBRARY}
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/_deps/ngtcp2-install
        -DCMAKE_INSTALL_LIBDIR:PATH=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DENABLE_DEBUG:BOOL=${ENABLE_NGHTTP_DEBUG}
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_STATIC_LIB:BOOL=ON
        -DENABLE_SHARED_LIB:BOOL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
        -DCMAKE_C_FLAGS:STRING=-fPIC
        -DCMAKE_CXX_FLAGS:STRING=-fPIC
)

add_library(ngtcp2-static STATIC IMPORTED GLOBAL)
target_link_libraries(ngtcp2-static INTERFACE OpenSSL::SSL OpenSSL::Crypto)
set_target_properties(ngtcp2-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/include"
)
add_dependencies(ngtcp2-static NgTcp2Project)

add_library(ngtcp2-crypto-ossl-static STATIC IMPORTED GLOBAL)
target_link_libraries(ngtcp2-crypto-ossl-static INTERFACE OpenSSL::SSL OpenSSL::Crypto)
set_target_properties(ngtcp2-crypto-ossl-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/${CMAKE_INSTALL_LIBDIR}/libngtcp2_crypto_ossl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/ngtcp2-install/include"
)
add_dependencies(ngtcp2-crypto-ossl-static NgTcp2Project)

ExternalProject_Add(Nghttp3Project
        GIT_REPOSITORY https://github.com/ngtcp2/nghttp3.git
        GIT_TAG v1.10.1
        GIT_SHALLOW ON
        BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/_deps/nghttp3-install/${CMAKE_INSTALL_LIBDIR}/libnghttp3.a
        UPDATE_COMMAND ""
        CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/_deps/nghttp3-install
        -DCMAKE_INSTALL_LIBDIR:PATH=${CMAKE_INSTALL_LIBDIR}
        -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
        -DENABLE_LIB_ONLY:BOOL=ON
        -DENABLE_DEBUG:BOOL=${ENABLE_NGHTTP_DEBUG}
        -DBUILD_STATIC_LIBS:BOOL=ON
        -DBUILD_SHARED_LIBS:BOOL=OFF
        -DENABLE_STATIC_LIB:BOOL=ON
        -DENABLE_SHARED_LIB:BOOL=OFF
        -DENABLE_DOC:BOOL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
        -DCMAKE_C_FLAGS:STRING=-fPIC
        -DCMAKE_CXX_FLAGS:STRING=-fPIC
)
add_library(nghttp3-static STATIC IMPORTED GLOBAL)
target_link_libraries(nghttp3-static INTERFACE ngtcp2-static ngtcp2-crypto-ossl-static)
set_target_properties(nghttp3-static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/_deps/nghttp3-install/${CMAKE_INSTALL_LIBDIR}/libnghttp3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/_deps/nghttp3-install/include"
)
add_dependencies(nghttp3-static Nghttp3Project)

add_library(mustache INTERFACE)
target_include_directories(mustache INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/kainjow)
add_library(mustache::mustache ALIAS mustache)

FetchContent_Declare(
        zeromq
        GIT_REPOSITORY https://github.com/zeromq/libzmq.git
        GIT_TAG 7a7bfa10e6b0e99210ed9397369b59f9e69cef8e # latest as of 2025-09-10 (fixes CMake < 3.5 deprecation)
)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "Build the tests for ZeroMQ")

# suppress warnings for missing zeromq dependencies by disabling some features
set(WITH_TLS OFF CACHE BOOL "TLS support for ZeroMQ WebSockets")
set(BUILD_SHARED OFF CACHE BOOL "Build cmake shared library")
option(WITH_PERF_TOOL "Build with perf-tools" OFF)

FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.28.0
)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "Disable zstd for httplib")

set(CPR_USE_SYSTEM_CURL ON CACHE BOOL "Use system libcurl in CPR" FORCE)
set(cpr_patch_command )
FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.14.1
        PATCH_COMMAND git apply --ignore-space-change --ignore-whitespace "${CMAKE_CURRENT_LIST_DIR}/patches/cpr-disable-std-fs-test.diff" || true
)

FetchContent_MakeAvailable(cpp-httplib zeromq cpr)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x
