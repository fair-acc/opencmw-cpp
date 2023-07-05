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

