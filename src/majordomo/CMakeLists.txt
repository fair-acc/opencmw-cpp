# setup header only library
add_library(majordomo INTERFACE
    include/majordomo/Broker.hpp
    include/majordomo/MockClient.hpp
    include/majordomo/Constants.hpp
    include/majordomo/Cryptography.hpp
    include/majordomo/Rbac.hpp
    include/majordomo/RestBackend.hpp
    include/majordomo/Settings.hpp
    include/majordomo/SubscriptionMatcher.hpp
    include/majordomo/Worker.hpp
)
target_include_directories(majordomo INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(majordomo
        INTERFACE
            core
            serialiser
            zmq
            httplib::httplib
            #OpenSSL::SSL
            pthread
            sodium
        )

install(
        TARGETS majordomo
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(OPENCMW_ENABLE_TESTING)
  add_subdirectory(test)
endif()
