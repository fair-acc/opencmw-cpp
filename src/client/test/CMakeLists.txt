include(CTest)
include(Catch)

add_executable(mockserver_tests catch_main.cpp MockServerTest.cpp)
target_link_libraries(mockserver_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 client)
# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(client_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(mockserver_tests)

add_executable(client_tests catch_main.cpp CmwClient_tests.cpp)
target_link_libraries(client_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 client)
# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(client_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(client_tests)

add_executable(clientPublisher_tests catch_main.cpp ClientPublisher_tests.cpp)
target_link_libraries(clientPublisher_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 client)
# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(client_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(clientPublisher_tests)
