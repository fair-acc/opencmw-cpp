add_executable(dns_tests dns_tests.cpp)

target_link_libraries(dns_tests PUBLIC services Catch2::Catch2)

target_include_directories(dns_tests PRIVATE ${CMAKE_SOURCE_DIR})

catch_discover_tests(dns_tests)


add_executable(oauthClient_tests OAuthClient_tests.cpp)

target_link_libraries(oauthClient_tests PUBLIC services Catch2::Catch2)

target_include_directories(oauthClient_tests PRIVATE ${CMAKE_SOURCE_DIR})

catch_discover_tests(oauthClient_tests)
