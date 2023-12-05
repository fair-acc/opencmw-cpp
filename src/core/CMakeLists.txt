# setup header only library
add_library(core INTERFACE)
target_include_directories(core INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(core INTERFACE $<BUILD_INTERFACE:mp-units::mp-units> refl-cpp::refl-cpp pthread)
set_target_properties(core PROPERTIES PUBLIC_HEADER "include/URI.hpp;include/MIME.hpp")

install(
        TARGETS core
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(OPENCMW_ENABLE_TESTING)
  add_subdirectory(test)
endif()
