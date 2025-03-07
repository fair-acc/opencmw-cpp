add_library(
  services
  INTERFACE
  include/services/dns.hpp
  include/services/dns_client.hpp
  include/services/dns_storage.hpp
  include/services/dns_types.hpp
  include/services/OAuthClient.hpp
  include/services/KeyStore.hpp)

target_include_directories(services INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                              $<INSTALL_INTERFACE:include/>)
target_link_libraries(services INTERFACE serialiser client sodium)

if(NOT EMSCRIPTEN)
  target_link_libraries(services INTERFACE majordomo httplib::httplib)
endif()

install(
  TARGETS services
  EXPORT opencmwTargets
  PUBLIC_HEADER DESTINATION include/opencmw)

if(OPENCMW_ENABLE_TESTING)
  add_subdirectory(test)
endif()
