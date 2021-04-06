include(${PROJECT_SOURCE_DIR}/cmake/ReflCpp.cmake)
add_executable(rttr_test rttr_test.cpp)
target_link_libraries(rttr_test PRIVATE project_options project_warnings CONAN_PKG::rttr)
