from conan import ConanFile
from conan.errors import ConanException
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, mkdir, replace_in_file
import os
import shutil


class MysqlConnectorCppJdbcConan(ConanFile):
    name = "mysql-connector-cpp-jdbc"
    version = "8.0.33"
    license = "GPL-2.0-only WITH Universal-FOSS-exception-1.0"
    url = "https://github.com/mysql/mysql-connector-cpp"
    description = "MySQL Connector/C++ 8 JDBC API (mysql_driver.h / cppconn)"
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
        self.requires("libmysqlclient/8.1.0", transitive_headers=True, transitive_libs=True)
        self.requires("openssl/3.3.2", transitive_libs=True)

    def build_requirements(self):
        # Oracle JDBC CMake still uses ARGN-as-variable; CMake 4 rejects that.
        self.tool_requires("cmake/[>=3.22 <4]")

    def source(self):
        get(self,
            "https://github.com/mysql/mysql-connector-cpp/archive/refs/tags/8.0.33.tar.gz",
            strip_root=True)

        jdbc_lists = os.path.join(self.source_folder, "jdbc", "CMakeLists.txt")
        # CMake's find_dependency() macro shadows Oracle's helper and would
        # return() from the top-level lists, producing an empty package.
        replace_in_file(
            self,
            jdbc_lists,
            "find_dependency(MySQL)",
            """find_package(OpenSSL REQUIRED)
if(NOT TARGET SSL::ssl)
  add_library(SSL::ssl INTERFACE IMPORTED)
  target_link_libraries(SSL::ssl INTERFACE OpenSSL::SSL)
endif()
if(NOT TARGET SSL::crypto)
  add_library(SSL::crypto INTERFACE IMPORTED)
  target_link_libraries(SSL::crypto INTERFACE OpenSSL::Crypto)
endif()
include(DepFindMySQL)""",
        )

        layout = os.path.join(self.source_folder, "jdbc", "install_layout.cmake")
        replace_in_file(
            self,
            layout,
            "if(jdbc_stand_alone)\n"
            "  # TODO: Manage install locations for stand-alone build.\n"
            "  return()\n"
            "endif()",
            "if(jdbc_stand_alone)\n"
            "  set(INSTALL_INCLUDE_DIR \"include\")\n"
            "  set(INSTALL_LIB_DIR \"lib\")\n"
            "  set(INSTALL_LIB_DIR_STATIC \"lib\")\n"
            "  set(INSTALL_DOC_DIR \"share/doc\")\n"
            "endif()",
        )
        replace_in_file(
            self,
            layout,
            'set(INSTALL_INCLUDE_DIR "${INSTALL_INCLUDE_DIR}/jdbc")',
            "if(NOT jdbc_stand_alone)\n"
            '  set(INSTALL_INCLUDE_DIR "${INSTALL_INCLUDE_DIR}/jdbc")\n'
            "endif()",
        )

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        mysql = self.dependencies["libmysqlclient"]
        mysql_root = mysql.package_folder.replace("\\", "/")
        tc.cache_variables["BUILD_STATIC"] = not self.options.shared
        tc.cache_variables["WITH_TESTS"] = False
        tc.cache_variables["BUNDLE_DEPENDENCIES"] = False
        tc.cache_variables["MYSQLCLIENT_STATIC_LINKING"] = True
        tc.cache_variables["MYSQLCLIENT_STATIC_BINDING"] = True
        tc.cache_variables["WITH_MYSQL"] = mysql_root
        tc.cache_variables["MYSQL_INCLUDE_DIR"] = os.path.join(mysql_root, "include")
        tc.cache_variables["MYSQL_LIB_DIR"] = os.path.join(mysql_root, "lib")
        tc.cache_variables["CMAKE_CXX_STANDARD"] = 17
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(build_script_folder="jdbc")
        # Only the JDBC driver objects. Oracle's merge_libraries would also
        # stuff libmysqlclient/OpenSSL/zlib into the archive and duplicate
        # symbols when the consumer already links those Conan packages.
        cmake.build(target="jdbc")

    def package(self):
        copy(self, "LICENSE*", self.source_folder, os.path.join(self.package_folder, "licenses"))
        jdbc = os.path.join(self.source_folder, "jdbc")
        inc = os.path.join(self.package_folder, "include")
        for header in ("mysql_driver.h", "mysql_connection.h", "mysql_error.h"):
            copy(self, header, os.path.join(jdbc, "driver"), inc, keep_path=False)
        copy(self, "*.h", os.path.join(jdbc, "cppconn"), os.path.join(inc, "cppconn"), keep_path=False)
        copy(self, "config.h", os.path.join(self.build_folder, "cppconn"),
             os.path.join(inc, "cppconn"), keep_path=False)
        copy(self, "version_info.h", os.path.join(self.build_folder, "cppconn"),
             os.path.join(inc, "cppconn"), keep_path=False)

        libdir = os.path.join(self.package_folder, "lib")
        mkdir(self, libdir)
        candidates = [
            os.path.join(self.build_folder, "driver", "libjdbc.a"),
            os.path.join(self.build_folder, "libjdbc.a"),
        ]
        src = next((path for path in candidates if os.path.isfile(path)), None)
        if src is None:
            raise ConanException("libjdbc.a was not built")
        shutil.copy2(src, os.path.join(libdir, "libmysqlcppconn-static.a"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "mysql-connector-cpp-jdbc")
        self.cpp_info.set_property("cmake_target_name", "mysql::jdbc")
        self.cpp_info.libs = ["mysqlcppconn-static"]
        if not self.options.shared:
            self.cpp_info.defines.append("STATIC_CONCPP")
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.extend(["pthread", "dl", "m", "resolv"])
