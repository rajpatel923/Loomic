# LoomicServer

Backend server for Loomic.

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.25 | [cmake.org](https://cmake.org/download/) |
| Ninja | any | [ninja-build.org](https://ninja-build.org/) — `brew install ninja` / `winget install Ninja-build.Ninja` |
| C++ compiler | C++20 support | Clang/GCC on macOS · MSVC or Clang on Windows |

---

## Build & Run

### 1. Configure

```bash
cmake --preset debug       # development (tests enabled)
cmake --preset release     # optimised build
```

### 2. Build

```bash
cmake --build --preset debug
cmake --build --preset release
```

### 3. Run

**macOS / Linux**
```bash
./build/debug/bin/LoomicServer
```

**Windows**
```powershell
.\build\debug\bin\LoomicServer.exe
```

---

## Running Tests

Tests are enabled automatically in the `debug` preset.

```bash
ctest --preset debug
```

---

## Project Structure

```
server/
├── CMakeLists.txt          # build definition
├── CMakePresets.json       # shared presets (debug / release)
├── src/                    # source files
├── include/LoomicServer/   # public headers
└── tests/                  # test suite
```
