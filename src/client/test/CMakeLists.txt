include(CTest)
include(Catch)

cmrc_add_resource_library(
        TEST_REST_ASSETS ALIAS test_assets_rest NAMESPACE assets
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/ca-cert.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/ca-key.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/client-cert.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/client-key.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/client-req.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/server-cert.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/server-key.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/server-req.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/password.txt
)

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

add_executable(rest_client_tests catch_main.cpp RestClient_tests.cpp)
target_link_libraries(rest_client_tests PUBLIC opencmw_project_warnings opencmw_project_options test_assets_rest Catch2::Catch2 client)
catch_discover_tests(rest_client_tests)

add_executable(clientPublisher_tests catch_main.cpp ClientPublisher_tests.cpp)
target_link_libraries(clientPublisher_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 client)
# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(client_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(clientPublisher_tests)
