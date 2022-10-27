add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(serialiser)
add_subdirectory(disruptor)

if (NOT USE_LIBCPP)
        # does not work, because <source_location> is missing
        add_subdirectory(majordomo)
        add_subdirectory(client)
endif()

# installation
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/opencmwConfigVersion.cmake"
        VERSION 0.0.1 # TODO: find a way to derive the version from git tag
        COMPATIBILITY AnyNewerVersion
)
include(CMakePackageConfigHelpers)
configure_package_config_file(
        cmake/opencmwConfig.cmake.in
        "${PROJECT_BINARY_DIR}/opencmwConfig.cmake"
        INSTALL_DESTINATION lib/cmake/opencmw
)
install(
        EXPORT opencmwTargets
        DESTINATION lib/cmake/opencmw
        NAMESPACE opencmw::
)
install(
        FILES ${CMAKE_BINARY_DIR}/opencmwConfig.cmake
        ${CMAKE_BINARY_DIR}/opencmwConfigVersion.cmake
        DESTINATION lib/cmake/opencmw
)
# copy refl-cpp header. todo: replace with conan dependency
# install(
#         FILES ${refl-cpp_SOURCE_DIR}/include/refl.hpp
#         DESTINATION include/opencmw
# )
