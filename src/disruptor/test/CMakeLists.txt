include(CTest)
include(Catch)

set (TEST_SRCS main.cpp)
add_executable(disruptor_test ${TEST_SRCS})
target_link_libraries(
    disruptor_test
    DisruptorStatic
    project_warnings
    project_options
    pthread
    CONAN_PKG::catch2
    )


