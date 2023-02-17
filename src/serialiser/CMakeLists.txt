cmrc_add_resource_library(
        MUSTACHE_TEST_SERVER_ASSETS ALIAS assets::mustache NAMESPACE assets
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/default.mustache
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/Services.mustache
)

add_library(serialiser INTERFACE include/IoSerialiserYAML.hpp)
target_include_directories(serialiser INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(serialiser INTERFACE $<BUILD_INTERFACE: mp-units::mp-units mustache::mustache assets::mustache> refl-cpp::refl-cpp core utils)
# if (NOT EMSCRIPTEN AND USE_LIBCPP AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.0.0)
#         target_link_libraries(serialiser INTERFACE -lc++abi -lc++experimental)
# endif()
set_target_properties(serialiser PROPERTIES PUBLIC_HEADER "include/fast_float.h;include/IoBuffer.hpp;include/IoSerialiserYaS.hpp;include/IoSerialiserJson.hpp;include/IoSerialiserCmwLight.hpp;include/IoSerialiser.hpp;include/MultiArray.hpp;include/opencmw.hpp")

install(
        TARGETS serialiser
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING OR EMSCRIPTEN)
  add_subdirectory(test)
endif()
