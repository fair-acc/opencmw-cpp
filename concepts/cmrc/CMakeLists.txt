# This example demonstrates the CMake resources library - CMRC
#
# It is a nice option to include contents of existing files located somewhere
# in the source directory into the compiled
#
# While this does not really provide a real filesystem, and it does not work
# with iostreams, fopen and friends, it provides a file-system-like API for
# accessing the files embedded into the executable.

# The first step is to
#     include(cmake/CMakeRC.cmake)
# (commented out here as we have it in the top-level CMakeLists.txt file)

# You can add whatever you want in your CMakeLists.txt file - just as usual
add_definitions (-fexceptions)

# In order to create an assets library (or embedded files library),
# you use the following command. This library will be seen from the
# C++ code as cmrc::assets and you can use just assets to link
# it with your executable (see below)
cmrc_add_resource_library(
    EXAMPLE_ASSETS ALIAS assets NAMESPACE assets
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    )

# Again, normal CMake - we are defining an executable
add_executable(cmrc_example main.cpp)

# And linking it to all the libraries we use -- including
# the assets library
target_link_libraries(cmrc_example
        PUBLIC assets
        )

# Now go to the main.cpp file

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
