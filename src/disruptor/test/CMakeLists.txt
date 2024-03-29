include(CTest)
include(Catch)

add_executable(disruptor_waitstrategy_tests catch_main.cpp WaitStrategy_tests.cpp)
target_link_libraries(
  disruptor_waitstrategy_tests
  PRIVATE opencmw_project_warnings
          opencmw_project_options
          Catch2::Catch2
          disruptor)
catch_discover_tests(disruptor_waitstrategy_tests)

add_executable(disruptor_core_tests catch_main.cpp Disruptor_tests.cpp)
target_link_libraries(
  disruptor_core_tests
  PRIVATE opencmw_project_warnings
          opencmw_project_options
          Catch2::Catch2
          disruptor)
catch_discover_tests(disruptor_core_tests)

if(NOT EMSCRIPTEN)
  add_executable(
    disruptor_tests
    catch_main.cpp
    WaitStrategy_tests.cpp
    RingBuffer_tests.cpp
    Sequence_tests.cpp
    Setting_tests.cpp)
  target_link_libraries(
    disruptor_tests
    PRIVATE opencmw_project_warnings
            opencmw_project_options
            Catch2::Catch2
            disruptor)
  catch_discover_tests(disruptor_tests)
endif()
