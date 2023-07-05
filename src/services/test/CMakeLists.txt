
add_executable(dns_tests dns_tests.cpp)

target_link_libraries(dns_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2  serialiser core client)
if(NOT EMSCRIPTEN)
    target_link_libraries(dns_tests PUBLIC majordomo)
    target_include_directories(dns_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()

catch_discover_tests(dns_tests)