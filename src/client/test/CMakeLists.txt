include(CTest)
include(Catch)

if(NOT EMSCRIPTEN)
  cmrc_add_resource_library(
    TEST_REST_ASSETS
    ALIAS
    test_assets_rest
    NAMESPACE
    assets
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/ca-cert.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/ca-key.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/client-cert.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/client-key.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/client-req.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/server-cert.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/server-key.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/server-req.pem
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/password.txt)

  add_executable(mockserver_tests catch_main.cpp MockServerTest.cpp)
  target_link_libraries(
    mockserver_tests
    PUBLIC opencmw_project_warnings
           opencmw_project_options
           Catch2::Catch2
           client)
  # We need concepts to be visible
  target_include_directories(mockserver_tests PRIVATE ${CMAKE_SOURCE_DIR})
  # automatically discover tests that are defined in catch based test files you can modify the unittests. Set
  # TEST_PREFIX to whatever you want, or use different for different binaries catch_discover_tests(client_tests
  # TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
  catch_discover_tests(mockserver_tests)

  add_executable(client_tests catch_main.cpp CmwClient_tests.cpp)
  target_link_libraries(
    client_tests
    PUBLIC opencmw_project_warnings
           opencmw_project_options
           Catch2::Catch2
           client)
  # automatically discover tests that are defined in catch based test files you can modify the unittests. Set
  # TEST_PREFIX to whatever you want, or use different for different binaries catch_discover_tests(client_tests
  # TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
  catch_discover_tests(client_tests)

  add_executable(nghttp2_tests catch_main.cpp nghttp2_tests.cpp)
  target_link_libraries(
    nghttp2_tests
    PUBLIC opencmw_project_warnings
    opencmw_project_options
    test_assets_rest
    Catch2::Catch2
    client)
  catch_discover_tests(nghttp2_tests)

  add_executable(clientPublisher_tests catch_main.cpp ClientPublisher_tests.cpp)
  target_link_libraries(
    clientPublisher_tests
    PUBLIC opencmw_project_warnings
           opencmw_project_options
           Catch2::Catch2
           client)
  # automatically discover tests that are defined in catch based test files you can modify the unittests. Set
  # TEST_PREFIX to whatever you want, or use different for different binaries catch_discover_tests(client_tests
  # TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
  catch_discover_tests(clientPublisher_tests)
endif()

add_executable(rest_client_only_tests RestClientOnly_tests.cpp)
target_link_libraries(rest_client_only_tests PUBLIC opencmw_project_warnings opencmw_project_options client)
target_include_directories(rest_client_only_tests PRIVATE ${CMAKE_SOURCE_DIR})

# These tests require a different kind of invocation as they need the server running
# catch_discover_tests(rest_client_only_tests)

if(EMSCRIPTEN)
  set(EMSCRIPTEN_REST_CLIENT_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/emscripten_rest_client)
  add_executable(emscripten_rest_client_tests ${EMSCRIPTEN_REST_CLIENT_TEST_DIR}/EmscriptenRestClientTest.cpp)
  set_target_properties(
    emscripten_rest_client_tests
    PROPERTIES SUFFIX ".html"
               LINK_DEPENDS ${EMSCRIPTEN_REST_CLIENT_TEST_DIR}/node_setup.js)
  target_link_libraries(
    emscripten_rest_client_tests
    PUBLIC opencmw_project_warnings
           opencmw_project_options
           client)
  target_link_options(
    emscripten_rest_client_tests
    PRIVATE
    --emrun
    --pre-js=${EMSCRIPTEN_REST_CLIENT_TEST_DIR}/node_setup.js)

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(REST_CLIENT_TEST_RUNNER ${EMSCRIPTEN_REST_CLIENT_TEST_DIR}/run.py)

  add_test(
    NAME emscripten_rest_client_tests_node
    COMMAND ${Python3_EXECUTABLE} ${REST_CLIENT_TEST_RUNNER} --mode node --node ${CMAKE_CROSSCOMPILING_EMULATOR}
            --binary $<TARGET_FILE_DIR:emscripten_rest_client_tests>/emscripten_rest_client_tests.js)
  set_tests_properties(emscripten_rest_client_tests_node PROPERTIES TIMEOUT 60)

  option(OPENCMW_REQUIRE_BROWSER_TESTS "Fail configuration when the Emscripten browser tests cannot be registered" OFF)
  get_filename_component(EMSCRIPTEN_TOOLS_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
  find_program(EMRUN_EXECUTABLE NAMES emrun HINTS ${EMSCRIPTEN_TOOLS_DIR})
  find_program(CHROME_EXECUTABLE NAMES google-chrome google-chrome-stable chromium chromium-browser)
  find_program(FIREFOX_EXECUTABLE NAMES firefox firefox-esr HINTS /snap/firefox/current/usr/lib/firefox)
  if(CHROME_EXECUTABLE AND EMRUN_EXECUTABLE)
    add_test(
      NAME emscripten_rest_client_tests_chrome
      COMMAND ${Python3_EXECUTABLE} ${REST_CLIENT_TEST_RUNNER} --mode browser --browser-family chromium --browser
              ${CHROME_EXECUTABLE} --emrun ${EMRUN_EXECUTABLE} --binary $<TARGET_FILE:emscripten_rest_client_tests>)
    set_tests_properties(emscripten_rest_client_tests_chrome PROPERTIES TIMEOUT 120)
  endif()
  if(FIREFOX_EXECUTABLE AND EMRUN_EXECUTABLE)
    add_test(
      NAME emscripten_rest_client_tests_firefox
      COMMAND ${Python3_EXECUTABLE} ${REST_CLIENT_TEST_RUNNER} --mode browser --browser-family firefox --browser
              ${FIREFOX_EXECUTABLE} --emrun ${EMRUN_EXECUTABLE} --binary $<TARGET_FILE:emscripten_rest_client_tests>)
    set_tests_properties(emscripten_rest_client_tests_firefox PROPERTIES TIMEOUT 120)
  endif()
  if(NOT EMRUN_EXECUTABLE OR (NOT CHROME_EXECUTABLE AND NOT FIREFOX_EXECUTABLE))
    if(OPENCMW_REQUIRE_BROWSER_TESTS)
      message(FATAL_ERROR "OPENCMW_REQUIRE_BROWSER_TESTS is set but emrun or a supported browser was not found")
    else()
      message(STATUS "emrun or a supported browser was not found - browser REST client tests not registered")
    endif()
  endif()

  add_executable(emscripten_client_tests EmscriptenClientTests.cpp)
  target_link_libraries(
    emscripten_client_tests
    PUBLIC opencmw_project_warnings
           opencmw_project_options
           client
           services)
  target_include_directories(emscripten_client_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_compile_options(emscripten_client_tests PRIVATE -DFETCH_DEBUG)
  target_link_options(
    emscripten_client_tests
    PRIVATE
    --pre-js=${CMAKE_CURRENT_SOURCE_DIR}/pre.js
    --emrun)
endif()
