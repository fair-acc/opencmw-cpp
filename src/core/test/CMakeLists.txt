include(CTest)
include(Catch)
add_executable(core_tests catch_main.cpp 00_opencmw_basic_tests.cpp collection_tests.cpp URI_tests.cpp MIME_tests.cpp TimingCtx_tests.cpp ThreadAffinity_tests.cpp)
target_link_libraries(core_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 core pthread)
# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(core_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(core_tests)
