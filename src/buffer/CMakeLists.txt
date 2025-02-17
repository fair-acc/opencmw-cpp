# setup header only library
add_library(buffer INTERFACE)
target_include_directories(buffer INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(buffer INTERFACE $<BUILD_INTERFACE:fmt::fmt-header-only> core pthread)

install(
  TARGETS buffer
  EXPORT opencmwTargets
  PUBLIC_HEADER DESTINATION include/opencmw)

# setup tests
if(OPENCMW_ENABLE_TESTING)
  add_subdirectory(test)
endif()
