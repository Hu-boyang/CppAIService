from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get
import os


class SimpleAmqpClientConan(ConanFile):
    name = "simpleamqpclient"
    version = "2.5.1.2026"
    license = "MIT"
    url = "https://github.com/alanxz/SimpleAmqpClient"
    description = "Simple C++ wrapper around rabbitmq-c"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "library"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        self.options["rabbitmq-c"].shared = self.options.shared

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        self.requires("rabbitmq-c/0.14.0", transitive_headers=True, transitive_libs=True)
        self.requires("boost/1.86.0", transitive_headers=True)

    def source(self):
        get(self, "https://github.com/alanxz/SimpleAmqpClient/archive/refs/heads/master.tar.gz",
            strip_root=True)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["BUILD_SHARED_LIBS"] = self.options.shared
        tc.cache_variables["ENABLE_TESTING"] = False
        tc.cache_variables["BUILD_API_DOCS"] = False
        tc.cache_variables["CMAKE_CXX_STANDARD"] = 17
        tc.cache_variables["CMAKE_CXX_STANDARD_REQUIRED"] = True
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "SimpleAmqpClient")
        self.cpp_info.set_property("cmake_target_name", "SimpleAmqpClient::SimpleAmqpClient")
        self.cpp_info.libs = ["SimpleAmqpClient"]
        if not self.options.shared:
            self.cpp_info.defines.append("SimpleAmqpClient_STATIC")
