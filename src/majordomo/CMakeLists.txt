set(HEADERS
  include/majordomo/Broker.hpp
  include/majordomo/MockClient.hpp
  include/majordomo/Constants.hpp
  include/majordomo/Cryptography.hpp
  include/majordomo/Http2Server.hpp
  include/majordomo/Rbac.hpp
  include/majordomo/Rest.hpp
  include/majordomo/Settings.hpp
  include/majordomo/SubscriptionMatcher.hpp
  include/majordomo/Worker.hpp
)

set(SOURCES
  include/majordomo/shared.cc
  include/majordomo/siphash.cc
  include/majordomo/tls_server_context_ossl.cc
  include/majordomo/tls_session_base_ossl.cc
  include/majordomo/tls_server_session_ossl.cc
  include/majordomo/util.cc
  include/majordomo/util_openssl.cc
)

# TODO(Frank) switch back to header only
add_library(majordomo STATIC ${SOURCES})

target_include_directories(majordomo INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)

target_link_libraries(majordomo
  PUBLIC # TODO(Frank) INTERFACE
  core
  serialiser
  zmq
  nghttp2
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
