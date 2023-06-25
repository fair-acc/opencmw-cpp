
add_executable(dns_tests dns_tests.cpp)

target_link_libraries(dns_tests PUBLIC services Catch2::Catch2)

target_include_directories(dns_tests PRIVATE ${CMAKE_SOURCE_DIR})

catch_discover_tests(dns_tests)