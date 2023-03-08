# gnutls: optional zeromq dependency for WSS (secure websockets)

# optionally required by zeromq, otherwise uses vendored tweetnacl
# FetchContent_Declare(
#         libsodium
#         GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
#         GIT_TAG 1.0.18
# )

FetchContent_Declare(
        zeromq
        GIT_REPOSITORY https://github.com/zeromq/libzmq.git
        GIT_TAG v4.3.4 # latest v4.3.4
)
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "Build the tests for ZeroMQ")
option(WITH_PERF_TOOL "Build with perf-tools" OFF)

FetchContent_Declare(
        cpp-httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.11.2 # latest v0.11.2
)

# zlib: optional httplib dependency
FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.2.12 # latest v1.2.12
)

# optional dependency of httplib
FetchContent_Declare(
        openssl
        GIT_REPOSITORY https://github.com/openssl/openssl.git
        GIT_TAG openssl-3.0.5 # latest openssl-3.0.5
)

# mustache is forked into 3rd_party/
# FetchContent_Declare(
#         kainjow-mustache
#         GIT_REPOSITORY https://github.com/kainjow/Mustache.git
#         GIT_TAG v4.1
# )
add_library(mustache INTERFACE)
target_include_directories(mustache INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/3rd_party/kainjow)
add_library(mustache::mustache ALIAS mustache)

set(RXCPP_DISABLE_TESTS_AND_EXAMPLES 1)
FetchContent_Declare(
        rxcpp
        GIT_REPOSITORY https://github.com/ReactiveX/RxCpp
        GIT_TAG 7f97aa901701343593869acad1ee5a02292f39cf # TODO Change to the latest tested release of RxCpp before making a release of OpenCMW
)
FetchContent_MakeAvailable(rxcpp)

FetchContent_MakeAvailable(cpp-httplib zeromq) # libsodium openssl kainjow-mustache
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/contrib) # replace contrib by extras for catch2 v3.x.x
