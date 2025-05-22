# setup header only library
add_library(disruptor INTERFACE)
target_include_directories(disruptor INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                               $<INSTALL_INTERFACE:include/>)
if(EMSCRIPTEN)
  target_link_libraries(disruptor INTERFACE core pthread)
else()
  target_link_libraries(disruptor INTERFACE core pthread)
endif()

install(
  TARGETS disruptor
  EXPORT opencmwTargets
  PUBLIC_HEADER DESTINATION include/opencmw)

# setup tests
if(OPENCMW_ENABLE_TESTING)
  add_subdirectory(test)
endif()
