include(CTest)
include(Catch)

set (RXCPP_DISABLE_TESTS_AND_EXAMPLES 1)
FetchContent_Declare(
    Rx
    GIT_REPOSITORY https://github.com/ReactiveX/RxCpp
    GIT_TAG        7f97aa901701343593869acad1ee5a02292f39cf # TODO Change to the latest tested release of RxCpp before making a release of OpenCMW
    # Silence warnings when using rxcpp by exporting the header directories as system includes TODO: upstream this change
    PATCH_COMMAND sed -e "s%target_include_directories(rxcpp INTERFACE%target_include_directories(rxcpp SYSTEM INTERFACE%" -i projects/CMake/CMakeLists.txt
)
FetchContent_MakeAvailable(Rx)

add_executable(disruptor_waitstrategy_tests catch_main.cpp WaitStrategy_tests.cpp)
target_link_libraries(disruptor_waitstrategy_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 disruptor core)
catch_discover_tests(disruptor_waitstrategy_tests)

add_executable(disruptor_tests catch_main.cpp WaitStrategy_tests.cpp RingBuffer_tests.cpp)
target_link_libraries(disruptor_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 core disruptor)
catch_discover_tests(disruptor_tests)

add_executable(disruptor_core_tests catch_main.cpp Disruptor_tests.cpp)
target_link_libraries(disruptor_core_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 disruptor)
catch_discover_tests(disruptor_core_tests)

add_executable(disruptor_rx_tests catch_main.cpp Rx_tests.cpp)
target_link_libraries(disruptor_rx_tests PRIVATE project_warnings project_options CONAN_PKG::catch2 disruptor rxcpp)
catch_discover_tests(disruptor_rx_tests)
