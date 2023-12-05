# setup header only library
add_library(
  client
  INTERFACE
  include/MockServer.hpp
  include/Client.hpp
  include/ClientContext.hpp
  include/RestClient.hpp)
target_include_directories(client INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                            $<INSTALL_INTERFACE:include/>)
if(NOT EMSCRIPTEN)
  target_link_libraries(
    client
    INTERFACE pthread
              core
              disruptor
              serialiser
              majordomo
              zmq)
else()
  target_link_libraries(
    client
    INTERFACE pthread
              core
              disruptor
              serialiser)
endif()
set_target_properties(
  client PROPERTIES PUBLIC_HEADER
                    "include/MockServer.hpp;include/Client.hpp;include/ClientContext.hpp;include/RestClient.hpp")

install(
  TARGETS client
  EXPORT opencmwTargets
  PUBLIC_HEADER DESTINATION include/opencmw)

# setup tests
if(OPENCMW_ENABLE_TESTING)
  add_subdirectory(test)
endif()
