include(../../../cmake/ReflCpp.cmake)
add_executable(refl_cpp_benchmark refl-cpp.cpp)
target_link_libraries(refl_cpp_benchmark PRIVATE project_options project_warnings)
target_include_directories(refl_cpp_benchmark PUBLIC ${refl-cpp_SOURCE_DIR})
