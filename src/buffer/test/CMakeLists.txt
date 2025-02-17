include(CTest)
include(Catch)

add_executable(buffer_tests catch_main.cpp buffer_tests.cpp atomic_bitset_tests.cpp)
target_link_libraries(buffer_tests PRIVATE opencmw_project_warnings opencmw_project_options Catch2::Catch2 buffer)
catch_discover_tests(buffer_tests)
