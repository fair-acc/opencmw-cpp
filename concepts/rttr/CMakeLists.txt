add_executable(rttr_test rttr_test.cpp)
target_compile_options(rttr_test PRIVATE -fno-rtti)
target_link_libraries(rttr_test PRIVATE project_options project_warnings CONAN_PKG::rttr)
