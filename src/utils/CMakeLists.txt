# setup header only library
add_library(utils INTERFACE
    include/utils/ZmqUtils.hpp
)

target_include_directories(utils INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(utils
        INTERFACE
            core
            libzmq
        )

install(
        TARGETS utils
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)
