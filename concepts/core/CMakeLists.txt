if (NOT USE_LIBCPP)
        add_executable(BasicThreadPool_example BasicThreadPool_example.cpp)
        target_link_libraries(BasicThreadPool_example PRIVATE core opencmw_project_options opencmw_project_warnings)
endif()

add_executable(collection_example collection_example.cpp)
target_link_libraries(collection_example PRIVATE core opencmw_project_options opencmw_project_warnings)

add_executable(URI_example URI_example.cpp)
target_link_libraries(URI_example PRIVATE core opencmw_project_options opencmw_project_warnings)
