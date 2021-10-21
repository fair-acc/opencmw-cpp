include(CTest)
include(Catch)

add_definitions (-fexceptions)

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
    )

catch_discover_tests(majordomo_tests)
