add_library(majordomo INTERFACE
  include/majordomo/Broker.hpp
  include/majordomo/MockClient.hpp
  include/majordomo/Constants.hpp
  include/majordomo/Cryptography.hpp
  include/majordomo/NgTcp2Util.hpp
  include/majordomo/Rbac.hpp
  include/majordomo/Rest.hpp
  include/majordomo/RestServer.hpp
  include/majordomo/Settings.hpp
  include/majordomo/SubscriptionMatcher.hpp
  include/majordomo/Worker.hpp
  include/majordomo/TlsServerSession_Ossl.hpp
  include/majordomo/TlsSessionBase_Ossl.hpp
)

target_include_directories(majordomo INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)

target_link_libraries(majordomo
  INTERFACE
  core
  serialiser
  zmq
  pthread
  rest
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
