from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, replace_in_file
import os


class MuduoConan(ConanFile):
    name = "muduo"
    version = "2.0.2"
    license = "BSD-3-Clause"
    url = "https://github.com/chenshuo/muduo"
    description = "Chen Shuo's event-driven C++ network library"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "library"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        self.requires("boost/1.86.0", transitive_headers=True)

    def source(self):
        get(self, "https://github.com/chenshuo/muduo/archive/refs/tags/v2.0.2.tar.gz",
            strip_root=True)
        cmakelists = os.path.join(self.source_folder, "CMakeLists.txt")
        replace_in_file(self, cmakelists,
                        "cmake_minimum_required(VERSION 2.6)",
                        "cmake_minimum_required(VERSION 3.5)")
        replace_in_file(self, cmakelists,
                        "option(MUDUO_BUILD_EXAMPLES \"Build Muduo examples\" ON)",
                        "option(MUDUO_BUILD_EXAMPLES \"Build Muduo examples\" OFF)")
        replace_in_file(self, cmakelists, "-Werror", "")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["MUDUO_BUILD_EXAMPLES"] = False
        tc.cache_variables["CMAKE_POLICY_VERSION_MINIMUM"] = "3.5"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "COPYRIGHT", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "*.h", os.path.join(self.source_folder, "muduo"),
             os.path.join(self.package_folder, "include", "muduo"), keep_path=True)
        for pattern in ("*.a", "*.so", "*.so.*", "*.dylib", "*.lib"):
            copy(self, pattern, self.build_folder, os.path.join(self.package_folder, "lib"),
                 keep_path=False)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "muduo")
        self.cpp_info.set_property("cmake_target_name", "muduo::muduo")
        self.cpp_info.libs = ["muduo_net", "muduo_base"]
        self.cpp_info.system_libs = ["pthread"]
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.append("rt")
