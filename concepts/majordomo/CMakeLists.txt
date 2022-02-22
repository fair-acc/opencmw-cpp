cmrc_add_resource_library(
    REST_SERVER_EXAMPLE_ASSETS ALIAS assets::rest NAMESPACE assets
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/error.mustache
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/main.css
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/english.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/german.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_b.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_b.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_w.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/img/logo_w.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/default.mustache
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/mustache/ServicesList.mustache
    )

cmrc_add_resource_library(
    TESTIMAGE_ASSETS ALIAS assets::testImages NAMESPACE testImages
    ${CMAKE_CURRENT_SOURCE_DIR}/testImages/PM5544_test_signal.png
    ${CMAKE_CURRENT_SOURCE_DIR}/testImages/SMPTE_Color_Bars.png
    )

message("COPY ${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR}" )
file(COPY "${CMAKE_SOURCE_DIR}/demo_sslcert/demo_private.key" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")
file(COPY "${CMAKE_SOURCE_DIR}/demo_sslcert/demo_public.crt" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/")

add_executable(MajordomoRest_example MajordomoRest_example.cpp)
target_include_directories(MajordomoRest_example
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
target_link_libraries(MajordomoRest_example PRIVATE majordomo opencmw_project_options opencmw_project_warnings assets::rest assets::testImages)
