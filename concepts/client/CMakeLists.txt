if (NOT EMSCRIPTEN)
add_executable(RestSubscription_example RestSubscription_example.cpp)
target_link_libraries(RestSubscription_example
    PRIVATE
        core
        client
        opencmw_project_options
        opencmw_project_warnings
        assets::rest)
endif ()

add_executable(RestSubscription_client RestSubscription_client.cpp)
target_link_libraries(RestSubscription_client
    PRIVATE
        core
        client
        opencmw_project_options
        opencmw_project_warnings
        )

add_executable(dns_example dns_example.cpp)
if (EMSCRIPTEN)
    target_link_libraries(dns_example PRIVATE services opencmw_project_options opencmw_project_warnings)
else ()
    target_include_directories(dns_example PRIVATE ${CMAKE_SOURCE_DIR}) # for majordomo helpers
    target_link_libraries(dns_example PRIVATE majordomo services opencmw_project_options opencmw_project_warnings assets::rest assets::testImages)
endif ()
