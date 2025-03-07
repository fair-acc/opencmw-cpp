# setup header only library
add_library(nghttp2 INTERFACE
    include/nghttp2/NgHttp2Utils.hpp
)

target_include_directories(nghttp2 INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(nghttp2
        INTERFACE
            nghttp2-static
        )

install(
        TARGETS nghttp2
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)
