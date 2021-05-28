# Automatically enable catch2 to generate ctest targets
if(CONAN_CATCH2_ROOT_DEBUG)
  include(${CONAN_CATCH2_ROOT_DEBUG}/lib/cmake/Catch2/Catch.cmake)
else()
  include(${CONAN_CATCH2_ROOT}/lib/cmake/Catch2/Catch.cmake)
endif()

# Include(FetchContent)
# FetchContent_Declare(
#         Catch2
#         GIT_REPOSITORY https://github.com/catchorg/Catch2.git
#         GIT_TAG        v2.13.6)
# alternative way to fetch catch 2 without using conan
# FetchContent_MakeAvailable(Catch2)
# Include(Catch2::Catch2.cmake)

include(${PROJECT_SOURCE_DIR}/cmake/ReflCpp.cmake)
#find_package(refl-cpp REQUIRED)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(../include/)

add_library(catch_main STATIC catch_main.cpp IoBuffer_tests.cpp IoSerialiser_tests.cpp IoClassSerialiser_tests.cpp ../include/opencmw.hpp ../include/IoBuffer.hpp ../include/IoSerialiser.hpp ../include/IoClassSerialiser.hpp ../include/MultiArray.hpp)
target_link_libraries(catch_main PUBLIC CONAN_PKG::catch2 CONAN_PKG::fmt)
target_link_libraries(catch_main PRIVATE project_options)
target_include_directories(catch_main SYSTEM PUBLIC ${refl-cpp_SOURCE_DIR}/include)

set(
  OpenCMW
  IoBuffer
  IoSerialiser
  IoClassSerialiser
  MultiArray
)

foreach(example IN LISTS OpenCMW)
  add_executable("${example}_tests" "${example}_tests.cpp")
  # target_link_libraries("${example}" PRIVATE refl-cpp::refl-cpp)
  target_link_libraries(
          "${example}_tests"
          PRIVATE project_warnings project_options catch_main)
  target_compile_features(
          "${example}_tests"
          PRIVATE cxx_std_20
  )
  target_include_directories("${example}_tests" SYSTEM PUBLIC ${refl-cpp_SOURCE_DIR})
  # automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX
  # to whatever you want, or use different for different binaries
  catch_discover_tests(
          "${example}_tests"
          TEST_PREFIX  "unittests."
          REPORTER xml
          OUTPUT_DIR .
          OUTPUT_PREFIX "unittests."
          OUTPUT_SUFFIX .xml
  )
endforeach()
