include(CTest)
include(Catch)

set (RXCPP_DISABLE_TESTS_AND_EXAMPLES 1)
FetchContent_Declare(
    Rx
    GIT_REPOSITORY https://github.com/ivan-cukic/wip-fork-rxcpp
    GIT_TAG        483963939e4a2adf674450dcb829005d84be1d59
)
FetchContent_MakeAvailable(Rx)

function(opencmw_add_test_app name sources)
    set (test_SRCS ${sources})
    message("Test sources: ${test_SRCS}")
    add_executable(${name} ${test_SRCS})

    target_include_directories (
        ${name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../disruptor/include
        )

    target_include_directories (${name} SYSTEM PUBLIC ${rx_SOURCE_DIR}/Rx/v2/src)

    target_link_libraries(${name}
        PUBLIC
            pthread
            project_warnings
            project_options
            CONAN_PKG::catch2
            CONAN_PKG::fmt
        )
endfunction()

function(opencmw_add_test_catch2 name sources)
    opencmw_add_test_app(${name} "${sources};catch_main.cpp")
    catch_discover_tests(${name})
endfunction()


opencmw_add_test_app(disruptor_rx_demo main.cpp)
target_compile_options(disruptor_rx_demo PRIVATE -fsanitize=address)
target_link_options(disruptor_rx_demo PRIVATE -fsanitize=address)

opencmw_add_test_catch2(disruptor_core_tests Disruptor_tests.cpp)
opencmw_add_test_catch2(disruptor_waitstrategy_tests WaitStrategy_tests.cpp)

