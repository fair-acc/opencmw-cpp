#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Conan recipe package for opencmw-cpp
"""

import os
from conans import ConanFile, CMake, tools


class OpencmwConan(ConanFile):
    """Conan recipe package for opencmw-cpp
    """
    name = "opencmw"
    version = "0.0.1"
    license = "LGPL-3.0"
    url = "https://github.com/fair-acc/opencmw-cpp"
    homepage = "https://github.com/fair-acc/opencmw-cpp"
    author = "Alexander Krimm"
    topics = (
        "conan", "middleware", "serialization", "reflection", "cpp20", "rest-api", "microservice",
        "opencmw"
    )
    description = "Open Common Middle-Ware library for accelerator equipment and beam-based control systems at FAIR."
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False]}
    default_options = {"shared": False}
    generators = "cmake"
    exports = "LICENSE"
    exports_sources = ["cmake/*", "concepts/*", "src/*", "CMakeLists.txt"]
    requires = "catch2/2.13.3", "fmt/7.1.3", "mp-units/0.7.0"

    def configure_cmake(self):
        """Create CMake instance and execute configure step
        """
        tools.mkdir("build")
        cmake = CMake(self)
        cmake.definitions["ENABLE_CONCEPTS"] = False
        cmake.definitions["ENABLE_TESTING"] = False
        cmake.configure(build_folder="build")
        return cmake

    def build(self):
        """Configure, build and install FlatBuffers using CMake.
        """
        cmake = self.configure_cmake()
        cmake.build()

    def package_info(self):
        """Collect built libraries names and solve flatc path.
        """
        self.cpp_info.libs = tools.collect_libs(self)
