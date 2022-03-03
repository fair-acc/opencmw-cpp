# setup header only library
add_library(majordomo INTERFACE)
target_include_directories(majordomo INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/>)
target_link_libraries(majordomo INTERFACE
    core
    serialiser
    CONAN_PKG::fmt
    CONAN_PKG::zeromq
    CONAN_PKG::mp-units
    CONAN_PKG::cpp-httplib
    CONAN_PKG::openssl
)
set_target_properties(majordomo PROPERTIES PUBLIC_HEADER "include/majordomo/Broker.hpp;include/majordomo/Client.hpp;include/majordomo/Constants.hpp;include/majordomo/Debug.hpp;include/majordomo/Filters.hpp;include/majordomo/Message.hpp;include/majordomo/QuerySerialiser.hpp;include/majordomo/Rbac.hpp;include/majordomo/RequestContext.hpp;include/majordomo/RestBackend.hpp;include/majordomo/Settings.hpp;include/majordomo/SubscriptionMatcher.hpp;include/majordomo/Utils.hpp;include/majordomo/Worker.hpp;include/majordomo/ZmqPtr.hpp")

install(
        TARGETS majordomo
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw/majordomo
)

# setup tests
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
