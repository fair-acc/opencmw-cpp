include(CTest)
include(Catch)

cmrc_add_resource_library(
    MUSTACHE_TEST_SERVER_ASSETS ALIAS assets::mustache NAMESPACE assets
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/default.mustache
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/Services.mustache
    )

add_executable(serialiser_tests catch_main.cpp IoBuffer_tests.cpp IoSerialiserYaS_tests.cpp IoSerialiserJson_tests.cpp IoSerialiserCmwLight_tests.cpp MultiArray_tests.cpp Utils_tests.cpp IoSerialiserYAML_tests.cpp MustacheSerialiser_tests.cpp 00_opencmw_basic_tests.cpp)
target_link_libraries(serialiser_tests PUBLIC project_warnings project_options CONAN_PKG::catch2 serialiser assets::mustache)

# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(serialiser_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(serialiser_tests)
