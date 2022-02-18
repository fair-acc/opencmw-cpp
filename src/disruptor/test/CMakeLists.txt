include(CTest)
include(Catch)

function(opencmw_disruptor_add_test name sources)
    set(test_SRCS "${sources};catch_main.cpp")
    message("Test sources: ${test_SRCS}")
    add_executable(${name} ${test_SRCS})

    target_include_directories(
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

opencmw_disruptor_add_test(disruptor_waitstrategy_tests WaitStrategy_tests.cpp)

add_executable(disruptor_benchmark_tests Disruptor_tests.cpp)
target_include_directories(disruptor_benchmark_tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../disruptor/include)
target_link_libraries(disruptor_benchmark_tests PUBLIC project_warnings project_options CONAN_PKG::catch2 core)

add_executable(disruptor_tests catch_main.cpp WaitStrategy_tests.cpp)
target_include_directories(disruptor_tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../disruptor/include)
target_link_libraries(disruptor_tests PUBLIC project_warnings project_options CONAN_PKG::catch2 core)

# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX to whatever you want, or use different for different binaries
# catch_discover_tests(serialiser_tests TEST_PREFIX  "unittests." REPORTER xml OUTPUT_DIR . OUTPUT_PREFIX "unittests." OUTPUT_SUFFIX .xml)
catch_discover_tests(disruptor_tests)
