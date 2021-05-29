include(${PROJECT_SOURCE_DIR}/cmake/ReflCpp.cmake)

add_executable(IoClassSerialiserBenchmark IoClassSerialiserBenchmark.cpp IoClassSerialiserBenchmark.hpp)
target_link_libraries(IoClassSerialiserBenchmark PRIVATE project_options project_warnings CONAN_PKG::fmt)
target_include_directories(IoClassSerialiserBenchmark SYSTEM PUBLIC ${refl-cpp_SOURCE_DIR}/include ../../include)