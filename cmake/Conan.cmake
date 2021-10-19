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

    set(ENV{CONAN_REVISIONS_ENABLED} 1) # required for new conan center repository
    include(${CMAKE_BINARY_DIR}/conan.cmake)



     message(STATUS "If your conan version is older than 1.40.3, you need to manually update the conan certificate. see: https://github.com/conan-io/conan/issues/9695#issuecomment-931406912")
     message(STATUS "$ conan config install https://github.com/conan-io/conanclientcert.git")
     conan_add_remote( # in case of problems with conan repositories, try to delete ~/.conan/remotes.json
            NAME conan-center
            URL https://center.conan.io)
    # Make sure to use conanfile.py to define dependencies, to stay consistent
    conan_cmake_run(
            REQUIRES
            catch2/2.13.3
            fmt/7.1.3
            mp-units/0.7.0
            # refl-cpp/0.12.1 # could be used once there is a new release
            OPTIONS
            BASIC_SETUP
            CMAKE_TARGETS # individual targets to link to
            BUILD
            missing)
    # conan_cmake_configure(REQUIRES fmt/6.1.2 GENERATORS cmake_find_package)
    # conan_cmake_autodetect(settings)
    # conan_cmake_install(PATH_OR_REFERENCE . BUILD missing REMOTE conan-center SETTINGS ${settings})
  endif()
endmacro()
