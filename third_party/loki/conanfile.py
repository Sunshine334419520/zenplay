import json
import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout, CMakeDeps
from conan.tools.files import copy
from conan.tools.files import collect_libs

class LokiConan(ConanFile):
    name = "loki"
    user = "spider"
    version = "0.1.0"
    url = "https://gitlab.gz.cvte.cn/app_center/link/pc/cbb/loki"
    package_type = "library"

    author = "yangguang@cvte.com"
    copyright = "Copyright © 2020 GuangZhou Shirui Electronics Co., Ltd"

    settings = "os", "compiler", "build_type", "arch"

    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    exports_sources = "src/*", "test/*","CMakeLists.txt", "version.json"

    def set_version(self):
        f = open('version.json')
        version = json.load(f)
        self.version = str(version['major']) + '.' + str(version['minor']) + '.' + str(version['patch'])
        print("version: ", self.version)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
    
    def build_requirements(self):
        self.test_requires("gtest/1.11.0")

    def layout(self):
        cmake_layout(self)

    generators = "CMakeDeps"
    
    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    
    def package(self):
        cmake = CMake(self)
        cmake.install()

        if self.settings.os == "Windows":
            pdbSrcDir = os.path.join(os.path.join(self.build_folder, str(self.settings.build_type)))
            print(pdbSrcDir)
            copy(self, pattern="*.pdb", src=pdbSrcDir, dst=os.path.join(self.package_folder, "lib"), keep_path=False)

    def package_info(self):
        self.cpp_info.libs = collect_libs(self)

    def pacakge_id(self):
        self.info.requires.clear()


