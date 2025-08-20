import json
import os
import shutil
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout, CMakeDeps
from conan.tools.files import copy
from conan.tools.files import collect_libs

class LokiConan(ConanFile):
    name = "loki_module"
    user = "spider"
    version = "1.0.0"
    url = "https://gitlab.gz.cvte.cn/app_center/link/pc/cbb/loki"

    author = "yangguang@cvte.com"
    copyright = "Copyright © 2020 GuangZhou Shirui Electronics Co., Ltd"

    exports_sources = "src/*", "version.json"

    no_copy_source = True

    def set_version(self):
        f = open('version.json')
        version = json.load(f)
        self.version = str(version['major']) + '.' + str(version['minor']) + '.' + str(version['patch'])
        print("version: ", self.version)

    def package(self):
        print(os.path.join(self.source_folder, "src", "loki_export.h"))
        print(os.path.join(self.source_folder, "src", "callback.h"))
        copy(self, "*.h", os.path.join(self.source_folder, "src"), os.path.join(self.package_folder, "include/loki"))

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []

    def pacakge_id(self):
        self.info.requires.clear()


