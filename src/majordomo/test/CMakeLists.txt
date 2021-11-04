include(CTest)
include(Catch)

add_definitions (-fexceptions)

set (
    majordomo_testapp_SRCS
    testapp.cpp
    )
add_executable(
    majordomo_testapp
    ${majordomo_testapp_SRCS}
    )

target_include_directories (
    majordomo_testapp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../yaxxzmq/include
    )

target_link_libraries (
    majordomo_testapp
    PUBLIC
        zmq
        pthread
        project_warnings
        project_options
        CONAN_PKG::catch2
        CONAN_PKG::fmt
    )


set (
    majordomo_tests_SRCS
    catch_main.cpp
    majordomo_tests.cpp
    )
add_executable (
    majordomo_tests
    ${majordomo_tests_SRCS}
    )

target_include_directories (
    majordomo_tests
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../yaxxzmq/include
    )

target_link_libraries (
    majordomo_tests
    PUBLIC
        zmq
        project_warnings
        project_options
        CONAN_PKG::catch2
        CONAN_PKG::fmt
    )

catch_discover_tests(majordomo_tests)

set (
    majordomo_benchmark_SRCS
    catch_main.cpp
    majordomo_benchmark.cpp
    )
add_executable (
    majordomo_benchmark
    ${majordomo_benchmark_SRCS}
    )

target_include_directories (
    majordomo_benchmark
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../yaxxzmq/include
    )

target_link_libraries (
    majordomo_benchmark
    PUBLIC
        zmq
        project_warnings
        project_options
        CONAN_PKG::catch2
        CONAN_PKG::fmt
    )
