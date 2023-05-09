# setup header only library
add_library(zmq INTERFACE
    include/zmq/Message.hpp
    include/zmq/Ptr.hpp
)

target_include_directories(zmq INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(zmq
        INTERFACE
            core
            libzmq
            #OpenSSL::SSL
        )

install(
        TARGETS zmq
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)
