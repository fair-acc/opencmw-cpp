add_subdirectory(core)
add_subdirectory(serialiser)
add_subdirectory(client)
add_subdirectory(disruptor)
add_subdirectory(services)

if(EMSCRIPTEN)
  message("Disabled majordomo and mdp client module because they are not compatible with emscripten builds")
else()
  add_subdirectory(majordomo)
  add_subdirectory(zmq)
endif()

# installation
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/opencmwConfigVersion.cmake"
  VERSION 0.0.1 # TODO: find a way to derive the version from git tag
  COMPATIBILITY AnyNewerVersion)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/opencmwConfig.cmake.in "${PROJECT_BINARY_DIR}/opencmwConfig.cmake"
                              INSTALL_DESTINATION lib/cmake/opencmw)
install(
  EXPORT opencmwTargets
  DESTINATION lib/cmake/opencmw
  NAMESPACE opencmw::)
install(FILES ${CMAKE_BINARY_DIR}/opencmwConfig.cmake ${CMAKE_BINARY_DIR}/opencmwConfigVersion.cmake
        DESTINATION lib/cmake/opencmw)
# copy refl-cpp header. todo: replace with conan dependency install( FILES ${refl-cpp_SOURCE_DIR}/include/refl.hpp
# DESTINATION include/opencmw )
