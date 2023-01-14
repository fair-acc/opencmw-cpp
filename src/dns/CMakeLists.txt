add_library(dns INTERFACE include/dns.hpp)
target_include_directories(dns INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(dns
        INTERFACE
            utils
            core
            majordomo
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
# setup tests
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
