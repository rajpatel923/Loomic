# LoomicServer

Backend server for Loomic.

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.25 | [cmake.org](https://cmake.org/download/) |
| Ninja | any | `brew install ninja` / `winget install Ninja-build.Ninja` |
| C++ compiler | C++20 support | Clang/GCC on macOS · MSVC or Clang on Windows |
| Git | any | Required to bootstrap vcpkg |

vcpkg is included as a submodule and manages all C++ dependencies automatically — no separate install step needed.

## Build

```sh
# 1. Bootstrap vcpkg (first time only)
./vcpkg/bootstrap-vcpkg.sh        # macOS/Linux
# .\vcpkg\bootstrap-vcpkg.bat     # Windows

# 2. Configure (vcpkg installs dependencies automatically via CMakePresets.json)
cmake --preset debug

# 3. Build
cmake --build --preset debug

# 4. Run
./build/debug/bin/LoomicServer --config config/server.json

# 5. Health check (second terminal)
curl http://localhost:8080/health   # → {"status":"ok"}

# 6. Tests
ctest --preset debug
```

### Release build

```sh
cmake --preset release
cmake --build --preset release
```

## Dependencies

Managed via [vcpkg](https://vcpkg.io) (`vcpkg.json`):

- boost-asio, boost-beast, boost-system
- spdlog
- nlohmann-json
- openssl
- hiredis
- jwt-cpp
- prometheus-cpp
- libpqxx
- abseil
- gtest