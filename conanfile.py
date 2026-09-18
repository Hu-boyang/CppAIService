from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class CppAIServiceConan(ConanFile):
    """Consumer recipe: install C++ deps for CppAIService via Conan 2.

    First-time setup:
      pip install 'conan>=2,<3'
      conan profile detect --force
      conan create conan/recipes/muduo --build=missing
      conan create conan/recipes/simpleamqpclient --build=missing
      conan create conan/recipes/mysql-connector-cpp-jdbc --build=missing
      conan install . --build=missing -s build_type=Release -s compiler.cppstd=17
      cmake --preset conan-release
      cmake --build --preset conan-release
    """

    required_conan_version = ">=2.0.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "with_opencv": [True, False],
        "with_onnxruntime": [True, False],
    }
    default_options = {
        "with_opencv": False,
        "with_onnxruntime": False,
        "opencv/*:with_ffmpeg": False,
        "opencv/*:with_gtk": False,
        "opencv/*:with_wayland": False,
        "opencv/*:with_openexr": False,
        "opencv/*:with_quirc": False,
        "opencv/*:dnn": True,
        "opencv/*:highgui": True,
        "opencv/*:imgcodecs": True,
        "opencv/*:imgproc": True,
        "opencv/*:videoio": False,
        "opencv/*:gapi": False,
        "opencv/*:ml": False,
        "opencv/*:photo": False,
        "opencv/*:stitching": False,
        "opencv/*:video": False,
        "opencv/*:calib3d": False,
        "opencv/*:features2d": False,
        "opencv/*:flann": False,
        "opencv/*:objdetect": False,
        "libcurl/*:with_ssl": "openssl",
    }

    def layout(self):
        cmake_layout(self)

    def generate(self):
        # Keep the committed root CMakePresets.json. Conan's default
        # CMakeUserPresets.json would duplicate the conan-release name.
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.user_presets_path = False
        tc.generate()

    def requirements(self):
        self.requires("openssl/3.3.2")
        self.requires("libcurl/8.12.1")
        self.requires("nlohmann_json/3.11.3")
        self.requires("rabbitmq-c/0.14.0")
        self.requires("boost/1.86.0")
        self.requires("libmysqlclient/8.1.0")
        self.requires("muduo/2.0.2")
        self.requires("simpleamqpclient/2.5.1.2026")
        self.requires("mysql-connector-cpp-jdbc/8.0.33")
        if self.options.with_opencv:
            self.requires("opencv/4.10.0")
        if self.options.with_onnxruntime:
            self.requires("onnxruntime/1.18.1")
