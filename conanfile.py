from conan import ConanFile, tools
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout

class ZenPlayConan(ConanFile):
    name = "ZenPlay"
    settings = "os", "compiler", "build_type", "arch"
    #options = {
    #    "shared": [True, False],
    #    "fPIC": [True, False]
    #}

    def configure(self):
        self.options["qt"].shared = True
        self.options["qt"].qttools = True
        #del self.settings.compiler.cppstd  # Remove cppstd to avoid conflicts with Qt

    def requirements(self):
        self.requires("nlohmann_json/3.12.0")
        self.requires("qt/6.7.3")
        self.requires("sdl/2.32.2")
        self.requires("ffmpeg/7.1.1")
        self.requires("spdlog/1.15.1")
        self.requires("gtest/1.17.0")
        self.requires("fmt/12.0.0")
    def layout(self):
        cmake_layout(self)
    
    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()