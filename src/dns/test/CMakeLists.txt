include(CTest)
include(Catch)


add_executable(dns_tests dns_test.cpp)
target_link_libraries(dns_tests PRIVATE
opencmw_project_warnings
opencmw_project_options
dns 
majordomo
client
Catch2::Catch2
pthread
)

catch_discover_tests(dns_tests)


