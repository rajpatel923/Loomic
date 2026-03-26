# LoomicServer

Backend server for Loomic.

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.25 | [cmake.org](https://cmake.org/download/) |
| Ninja | any | [ninja-build.org](https://ninja-build.org/) — `brew install ninja` / `winget install Ninja-build.Ninja` |
| C++ compiler | C++20 support | Clang/GCC on macOS · MSVC or Clang on Windows |


To build and verify the milestone:

cd /Users/rajpatel/Documents/GitHub/Loomic/server

# 1. Install dependencies
conan install . --output-folder=build/debug  --build=missing -s build_type=Debug

conan install . --output-folder=build/release --build=missing -s build_type=Release

# 2. Configure
cmake --preset debug

# 3. Build
cmake --build --preset debug

# 4. Run
./build/debug/bin/LoomicServer --config config/server.json

# 5. Health check (second terminal)
curl http://localhost:8080/health   # → {"status":"ok"}

# 6. Tests
ctest --preset debug