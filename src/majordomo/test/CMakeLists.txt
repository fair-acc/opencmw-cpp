include(CTest)
include(Catch)

add_definitions (-fexceptions)

cmrc_add_resource_library(
    REST_SERVER_ASSETS ALIAS assets::rest NAMESPACE assets
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/error.mustache
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/main.css
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/english.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/german.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_b.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_b.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_w.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_w.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/default.mustache
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/ServicesList.mustache
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/ServiceNamesList.mustache
    )

message("COPY ${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}" )
file(COPY "${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key"
    DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/")
file(COPY "${CMAKE_SOURCE_DIR}/demo_sslcert/demo_public.crt"
    DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/")

function(opencmw_add_test_app name sources)
    set (test_SRCS ${sources})
    message("Test sources: ${test_SRCS}")
    add_executable(${name} ${test_SRCS})

    target_include_directories (
        ${name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../serialiser/include
        )

    target_link_libraries(${name}
        PUBLIC
            pthread
            project_warnings
            project_options
            serialiser
            CONAN_PKG::catch2
            CONAN_PKG::fmt
            CONAN_PKG::zeromq
            CONAN_PKG::mp-units
            CONAN_PKG::cpp-httplib
            CONAN_PKG::openssl
            assets::rest
        )
endfunction()

function(opencmw_add_test_catch2 name sources)
    opencmw_add_test_app(${name} "${sources};catch_main.cpp")
    catch_discover_tests(${name})
endfunction()

opencmw_add_test_app(majordomo_testapp testapp.cpp)
opencmw_add_test_app(majordomo_rest_server restserver_testapp.cpp)
opencmw_add_test_app(majordomo_benchmark majordomo_benchmark.cpp)

opencmw_add_test_catch2(majordomo_tests majordomo_tests.cpp;subscriptionmatcher_tests.cpp)
opencmw_add_test_catch2(majordomo_worker_tests majordomoworker_tests.cpp;subscriptionmatcher_tests.cpp)
opencmw_add_test_catch2(majordomo_worker_rest_tests majordomoworker_rest_tests.cpp;subscriptionmatcher_tests.cpp)
opencmw_add_test_catch2(queryserialiser_tests queryserialiser_tests.cpp)

