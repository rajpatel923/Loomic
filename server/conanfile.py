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
        self.requires("libpqxx/7.9.2")
        self.requires("libxcrypt/4.4.36")

    def configure(self):
        # Ensure the postgresql Conan package (libpq) is built with SSL support.
        # Without this, libpq is compiled without OpenSSL and sslmode=require fails.
        self.options["postgresql/*"].with_openssl = True

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()
