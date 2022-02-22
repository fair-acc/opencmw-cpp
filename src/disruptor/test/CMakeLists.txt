include(CTest)
include(Catch)

set (RXCPP_DISABLE_TESTS_AND_EXAMPLES 1)
FetchContent_Declare(
        Rx
        GIT_REPOSITORY https://github.com/ivan-cukic/wip-fork-rxcpp
        GIT_TAG        483963939e4a2adf674450dcb829005d84be1d59
)
FetchContent_MakeAvailable(Rx)

add_executable(disruptor_waitstrategy_tests catch_main.cpp WaitStrategy_tests.cpp)
target_link_libraries(disruptor_waitstrategy_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 disruptor core)
catch_discover_tests(disruptor_waitstrategy_tests)

add_executable(disruptor_tests catch_main.cpp WaitStrategy_tests.cpp RingBuffer_tests.cpp)
target_link_libraries(disruptor_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 core disruptor)
catch_discover_tests(disruptor_tests)

add_executable(disruptor_benchmark_tests Disruptor_tests.cpp)
target_link_libraries(disruptor_benchmark_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 disruptor)

add_executable(disruptor_rx_demo main.cpp)
target_link_libraries(disruptor_rx_demo PRIVATE project_warnings project_options core disruptor)
target_include_directories (disruptor_rx_demo SYSTEM PUBLIC ${rx_SOURCE_DIR}/Rx/v2/src)
target_compile_options(disruptor_rx_demo PRIVATE -fsanitize=address)
target_link_options(disruptor_rx_demo PRIVATE -fsanitize=address)
