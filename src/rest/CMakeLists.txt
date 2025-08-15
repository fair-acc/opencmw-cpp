add_library(rest INTERFACE
        include/rest/RestUtils.hpp
)

target_include_directories(rest INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(rest
        INTERFACE
        nghttp2-static
        nghttp3-static
)

install(
        TARGETS rest
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)
