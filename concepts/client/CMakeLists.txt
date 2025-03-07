add_executable(LoadTest_client LoadTest_client.cpp)
target_link_libraries(
  LoadTest_client
  PRIVATE core
          client
          opencmw_project_options
          opencmw_project_warnings)

add_executable(RestSubscription_client RestSubscription_client.cpp)
target_link_libraries(
  RestSubscription_client
  PRIVATE core
          client
          opencmw_project_options
          opencmw_project_warnings)

add_executable(dns_example dns_example.cpp)
if(EMSCRIPTEN)
  target_compile_options(dns_example PRIVATE -Wcast-function-type-strict)
  target_link_libraries(
    dns_example
    PRIVATE services
            opencmw_project_options
            opencmw_project_warnings
            --pre-js=${CMAKE_CURRENT_SOURCE_DIR}/pre.js
            --emrun)

else()
  target_include_directories(dns_example PRIVATE ${CMAKE_SOURCE_DIR}) # for majordomo helpers
  target_link_libraries(
    dns_example
    PRIVATE majordomo
            services
            opencmw_project_options
            opencmw_project_warnings
            assets::rest
            assets::testImages)
endif()
