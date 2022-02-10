# setup header only library
add_library(disruptor INTERFACE)
target_include_directories(disruptor INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/>)
target_link_libraries(disruptor INTERFACE CONAN_PKG::mp-units refl-cpp::refl-cpp utils pthread)
set_target_properties(disruptor PROPERTIES PUBLIC_HEADER "include/URI.hpp;include/MIME.hpp")

install(
        TARGETS disruptor
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING)
    add_subdirectory(test)
endif()