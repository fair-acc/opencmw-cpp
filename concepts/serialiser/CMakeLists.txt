include(${PROJECT_SOURCE_DIR}/cmake/ReflCpp.cmake)

add_executable(IoClassSerialiserBenchmark IoClassSerialiserBenchmark.cpp IoClassSerialiserBenchmark.hpp)
target_link_libraries(IoClassSerialiserBenchmark PRIVATE project_options project_warnings CONAN_PKG::fmt CONAN_PKG::mp-units)
target_include_directories(IoClassSerialiserBenchmark SYSTEM PUBLIC ${refl-cpp_SOURCE_DIR}/include ../../include)

add_executable(SimpleReflectionExample SimpleReflectionExample.cpp)
target_link_libraries(SimpleReflectionExample PRIVATE project_options project_warnings CONAN_PKG::fmt CONAN_PKG::mp-units)
target_include_directories(SimpleReflectionExample SYSTEM PUBLIC ${refl-cpp_SOURCE_DIR}/include ../../include)