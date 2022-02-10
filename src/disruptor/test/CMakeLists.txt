include(CTest)
include(Catch)

function(opencmw_disruptor_add_test name sources)
    set (test_SRCS "${sources};catch_main.cpp")
    message("Test sources: ${test_SRCS}")
    add_executable(${name} ${test_SRCS})

    target_include_directories (
        ${name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../../disruptor/include
        )

    target_link_libraries(${name}
        PUBLIC
            pthread
            project_warnings
            project_options
            CONAN_PKG::catch2
            CONAN_PKG::fmt
        )

    catch_discover_tests(${name})
endfunction()

opencmw_disruptor_add_test(disruptor_core_tests Disruptor_tests.cpp)
opencmw_disruptor_add_test(disruptor_waitstrategy_tests WaitStrategy_tests.cpp)

