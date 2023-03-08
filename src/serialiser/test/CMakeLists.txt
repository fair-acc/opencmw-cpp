include(CTest)
include(Catch)

set(serialiser_tests_SRCS catch_main.cpp IoBuffer_tests.cpp IoSerialiserYaS_tests.cpp IoSerialiserJson_tests.cpp IoSerialiserCmwLight_tests.cpp MultiArray_tests.cpp Utils_tests.cpp IoSerialiserYAML_tests.cpp queryserialiser_tests.cpp)
if (NOT EMSCRIPTEN)
    set(serialiser_tests_NATIVE_SRCS MustacheSerialiser_tests.cpp)
endif ()

add_executable(serialiser_tests ${serialiser_tests_SRCS} ${serialiser_tests_NATIVE_SRCS})
target_link_libraries(serialiser_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 serialiser)
if (EMSCRIPTEN)
    target_compile_options(serialiser_tests PUBLIC -Wno-shorten-64-to-32 -fwasm-exceptions)
    target_link_options(serialiser_tests PUBLIC -fwasm-exceptions)
endif ()
# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(serialiser_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(serialiser_tests)
