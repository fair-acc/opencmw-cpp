macro(run_conan)
  if(CONAN_EXPORTED) # in conan local cache
   # standard conan installation, deps will be defined in conanfile.py
   # and not necessary to call conan again, conan is already running
   message(STATUS "Performing build inside of conan cache")
   include(${CMAKE_BINARY_DIR}/../conanbuildinfo.cmake)
   conan_basic_setup(TARGETS)
  else() # in user space
     message(STATUS "Performing standalone build using cmake-conan")
    # Download automatically, you can also just copy the conan.cmake file
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
      message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
      file(DOWNLOAD "https://github.com/conan-io/cmake-conan/raw/v0.16.1/conan.cmake" "${CMAKE_BINARY_DIR}/conan.cmake")
    endif()

    set(ENV{CONAN_REVISIONS_ENABLED} 1) # required for new bincrafters repository
    include(${CMAKE_BINARY_DIR}/conan.cmake)

    conan_add_remote(
            NAME bincrafters
            URL https://bincrafters.jfrog.io/artifactory/api/conan/public-conan)
    # Make sure to use conanfile.py to define dependencies, to stay consistent
    conan_cmake_run(
            REQUIRES
            ${CONAN_EXTRA_REQUIRES}
            catch2/2.13.3
            fmt/7.1.3
            mp-units/0.7.0
            # refl-cpp/0.12.1 # could be used once there is a new release
            OPTIONS
            ${CONAN_EXTRA_OPTIONS}
            BASIC_SETUP
            CMAKE_TARGETS # individual targets to link to
            BUILD
            missing)
    # conan_cmake_configure(REQUIRES fmt/6.1.2 GENERATORS cmake_find_package)
    # conan_cmake_autodetect(settings)
    # conan_cmake_install(PATH_OR_REFERENCE . BUILD missing REMOTE conan-center SETTINGS ${settings})
  endif()
endmacro()
