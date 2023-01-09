
add_library(dns INTERFACE)

target_include_directories(dns INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(dns
        INTERFACE
            utils
            core
            serialiser
            httplib::httplib
            libzmq
            #OpenSSL::SSL
            pthread
        )
 set_target_properties(dns PROPERTIES PUBLIC_HEADER "include/dns.hpp")


install(
        TARGETS dns
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

