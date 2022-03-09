include(CTest)
include(Catch)

set (RXCPP_DISABLE_TESTS_AND_EXAMPLES 1)
FetchContent_Declare(
    Rx
    GIT_REPOSITORY https://github.com/ReactiveX/RxCpp
    GIT_TAG        7f97aa901701343593869acad1ee5a02292f39cf
    # TODO Change to the latest tested release of RxCpp before
    #      making a release of OpenCMW
)
FetchContent_MakeAvailable(Rx)

function(opencmw_add_test_app name sources)
    set (test_SRCS ${sources})
    message("Test sources: ${test_SRCS}")
    add_executable(${name} ${test_SRCS})

    target_link_libraries(${name}
        PUBLIC
            pthread
            project_warnings
            project_options
            rxcpp
            disruptor
            CONAN_PKG::catch2
            CONAN_PKG::fmt
        )
endfunction()

function(opencmw_add_test_catch2 name sources)
    opencmw_add_test_app(${name} "${sources};catch_main.cpp")
    catch_discover_tests(${name})
endfunction()

opencmw_add_test_catch2(disruptor_core_tests Disruptor_tests.cpp)
opencmw_add_test_catch2(disruptor_rx_tests Rx_tests.cpp)
opencmw_add_test_catch2(disruptor_waitstrategy_tests WaitStrategy_tests.cpp)

