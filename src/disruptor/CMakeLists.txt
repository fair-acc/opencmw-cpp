# setup header only library
add_library(disruptor INTERFACE)
target_include_directories(disruptor INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include/>)
target_link_libraries(disruptor INTERFACE $<BUILD_INTERFACE:fmt::fmt-header-only> core rxcpp pthread)

install(
        TARGETS disruptor
        EXPORT opencmwTargets
        PUBLIC_HEADER DESTINATION include/opencmw
)

# setup tests
if(ENABLE_TESTING)
    add_subdirectory(test)
endif()