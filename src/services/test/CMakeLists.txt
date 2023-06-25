
if(NOT EMSCRIPTEN)
    add_executable(dns_tests dns_tests.cpp)
    target_link_libraries(dns_tests PUBLIC opencmw_project_warnings opencmw_project_options Catch2::Catch2 majordomo serialiser core)
endif()