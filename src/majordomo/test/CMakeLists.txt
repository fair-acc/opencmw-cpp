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
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
    )

target_link_libraries (
    majordomo_testapp
    PUBLIC
        pthread
        project_warnings
        project_options
        CONAN_PKG::catch2
        CONAN_PKG::fmt
        CONAN_PKG::zeromq
    )


set (
    majordomo_tests_SRCS
    catch_main.cpp
    majordomo_tests.cpp
    majordomoworker_tests.cpp
    queryserialiser_tests.cpp
    rbac_tests.cpp
    subscriptionmatcher_tests.cpp
    )
add_executable (
    majordomo_tests
    ${majordomo_tests_SRCS}
    )

target_include_directories (
    majordomo_tests
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include ${CMAKE_CURRENT_SOURCE_DIR}/../../serialiser/include
    )

target_link_libraries (
    majordomo_tests
    PUBLIC
        pthread
        project_warnings
        project_options
        serialiser
        CONAN_PKG::catch2
        CONAN_PKG::fmt
        CONAN_PKG::mp-units
        CONAN_PKG::zeromq
    )

catch_discover_tests(majordomo_tests)

set (
    majordomo_benchmark_SRCS
    majordomo_benchmark.cpp
    )
add_executable (
    majordomo_benchmark
    ${majordomo_benchmark_SRCS}
    )

target_include_directories (
    majordomo_benchmark
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/include ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include
    )

target_link_libraries (
    majordomo_benchmark
    PUBLIC
        pthread
        project_warnings
        project_options
        CONAN_PKG::fmt
        CONAN_PKG::zeromq
    )
