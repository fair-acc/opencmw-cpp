# Automatically enable catch2 to generate ctest targets
if(CONAN_CATCH2_ROOT_DEBUG)
  include(${CONAN_CATCH2_ROOT_DEBUG}/lib/cmake/Catch2/Catch.cmake)
else()
  include(${CONAN_CATCH2_ROOT}/lib/cmake/Catch2/Catch.cmake)
endif()

add_library(catch_main STATIC catch_main.cpp IoBuffer_tests.cpp IoSerialiser_tests.cpp IoClassSerialiser_tests.cpp)
target_link_libraries(catch_main PUBLIC project_warnings project_options CONAN_PKG::catch2 serialiser)

set(
  OpenCMW
  IoBuffer
  IoSerialiser
  IoClassSerialiser
  MultiArray
)

foreach(example IN LISTS OpenCMW)
  add_executable("${example}_tests" "${example}_tests.cpp")
  target_link_libraries("${example}_tests" PRIVATE catch_main)
  # automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
  catch_discover_tests("${example}_tests" TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
endforeach()
