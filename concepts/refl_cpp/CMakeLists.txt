include(${PROJECT_SOURCE_DIR}/cmake/ReflCpp.cmake)
add_executable(refl_cpp_test refl_cpp_test.cpp)
target_link_libraries(refl_cpp_test PRIVATE project_options project_warnings)
target_include_directories(refl_cpp_test SYSTEM PUBLIC ${refl-cpp_SOURCE_DIR})
