from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps


class LoomicServerConan(ConanFile):
    name = "loomicserver"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("asio/1.30.2")
        self.requires("boost/1.86.0")
        self.requires("protobuf/5.27.0")
        self.requires("spdlog/1.14.1")
        self.requires("gtest/1.14.0")
        self.requires("abseil/20240116.2")
        self.requires("nlohmann_json/3.11.3")
        self.requires("openssl/3.3.1")
        self.requires("hiredis/1.2.0")
        self.requires("jwt-cpp/0.7.0")
        self.requires("prometheus-cpp/1.2.4")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
