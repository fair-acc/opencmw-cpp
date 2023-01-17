# setup header only library
add_library(majordomo INTERFACE
    include/majordomo/Broker.hpp
    include/majordomo/MockClient.hpp
    include/majordomo/Constants.hpp
    include/majordomo/Debug.hpp
    include/majordomo/Message.hpp
    include/majordomo/Rbac.hpp
    include/majordomo/RestBackend.hpp
    include/majordomo/Settings.hpp
    include/majordomo/SubscriptionMatcher.hpp
    include/majordomo/Utils.hpp
    include/majordomo/Worker.hpp
    include/majordomo/ZmqPtr.hpp
    include/majordomo/Dns.hpp
)
target_include_directories(majordomo INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(majordomo
        INTERFACE
            utils
            core
            serialiser
            httplib::httplib
            libzmq
            #OpenSSL::SSL
            pthread
        )

install(
        TARGETS majordomo
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
