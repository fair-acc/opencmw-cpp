# ensure dependencies are present
#include_directories(${refl-cpp_SOURCE_DIR})

# setup header only library
add_library(serialiser INTERFACE)
target_include_directories(serialiser INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/>)
target_link_libraries(serialiser INTERFACE CONAN_PKG::mp-units refl-cpp::refl-cpp utils)
set_target_properties(serialiser PROPERTIES PUBLIC_HEADER "include/IoBuffer.hpp;include/IoClassSerialiser.hpp;include/IoSerialiser.hpp;include/MultiArray.hpp;include/opencmw.hpp")

install(
        TARGETS serialiser
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
