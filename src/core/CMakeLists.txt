# setup header only library
add_library(core INTERFACE)
target_include_directories(core INTERFACE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/serialiser/include> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/>) #TODO: remove serialiser dependency once TimingCtx is moved
target_link_libraries(core INTERFACE CONAN_PKG::mp-units refl-cpp::refl-cpp utils pthread)
set_target_properties(core PROPERTIES PUBLIC_HEADER "include/URI.hpp;include/MIME.hpp")

install(
        TARGETS core
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
