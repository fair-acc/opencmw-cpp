include(CTest)
include(Catch)

add_definitions (-fexceptions)

set (
    yaz_tests_SRCS
    main.cpp
    test_instantiation_of.cpp
    test_tuple_for_each.cpp
    )

set (
    yaz_testapp_SRCS
    test.cpp
    )

add_executable (
    yaz_tests
    ${yaz_tests_SRCS}
    )

add_executable (
    yaz_testapp
    ${yaz_testapp_SRCS}
    )

target_include_directories (
    yaz_tests
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

target_include_directories (
    yaz_testapp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

target_link_libraries (
    yaz_tests
    PUBLIC
        zmq
        project_warnings
        project_options
        CONAN_PKG::catch2
    )

target_link_libraries (
    yaz_testapp
    PUBLIC
        zmq
        project_warnings
        project_options
    )

catch_discover_tests(yaz_tests)

