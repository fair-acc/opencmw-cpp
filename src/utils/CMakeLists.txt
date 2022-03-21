# setup header only library
add_library(utils INTERFACE)
target_include_directories(utils INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/opencmw/>)
target_link_libraries(utils INTERFACE $<BUILD_INTERFACE:fmt::fmt-header-only>)
set_target_properties(utils PROPERTIES PUBLIC_HEADER "include/Debug.hpp;include/Utils.hpp")

install(
        TARGETS utils
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
