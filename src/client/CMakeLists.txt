# setup header only library
add_library(client INTERFACE include/MockServer.hpp include/Client.hpp include/ClientContext.hpp)
target_include_directories(client INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/>)
target_link_libraries(client INTERFACE utils pthread core disruptor serialiser majordomo)
set_target_properties(client PROPERTIES PUBLIC_HEADER "include/MockServer.hpp;include/Client.hpp;include/ClientContext.hpp")

install(
TARGETS client
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING)
    add_subdirectory(test)
endif()
