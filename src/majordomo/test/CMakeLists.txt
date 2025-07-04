add_definitions (-fexceptions)

message("COPY ${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}" )
file(COPY "${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_SOURCE_DIR}/demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

function(opencmw_add_test_app name sources)
    set (test_SRCS ${sources})
    message("Test sources: ${test_SRCS}")
    add_executable(${name} ${test_SRCS})

    # We need concepts to be visible
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})

    target_link_libraries(${name}
        PUBLIC
            opencmw_project_warnings
            opencmw_project_options
            serialiser
            client
            majordomo
            Catch2::Catch2
            assets::rest
        )
endfunction()

function(opencmw_add_test_catch2 name sources)
    opencmw_add_test_app(${name} "${sources};catch_main.cpp")
    catch_discover_tests(${name})
endfunction()

opencmw_add_test_app(majordomo_testapp testapp.cpp)
opencmw_add_test_app(majordomo_benchmark majordomo_benchmark.cpp)

opencmw_add_test_catch2(base64pp_tests base64pp_tests.cpp)
opencmw_add_test_catch2(RBAC_tests rbac_tests.cpp)
opencmw_add_test_catch2(Cryptography_tests cryptography_tests.cpp)
opencmw_add_test_catch2(SubscriptionMatch_tests subscriptionmatcher_tests.cpp)
opencmw_add_test_catch2(majordomo_tests majordomo_tests.cpp;subscriptionmatcher_tests.cpp)
opencmw_add_test_catch2(majordomo_worker_tests majordomoworker_tests.cpp;subscriptionmatcher_tests.cpp)
opencmw_add_test_catch2(majordomo_worker_rest_tests majordomoworker_rest_tests.cpp;subscriptionmatcher_tests.cpp)

if(NOT ENSCRIPTEN)
    opencmw_add_test_catch2(majordomo_worker_load_tests majordomo_load_tests.cpp)
endif()
