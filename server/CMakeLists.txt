cmake_minimum_required(VERSION 3.25)

project(
    LoomicServer
    VERSION 0.1.0
    DESCRIPTION "Loomic backend server"
    LANGUAGES CXX
)

# ── Standard ──────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Output directories ────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(LoomicServer
    src/main.cpp
)

target_include_directories(LoomicServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ── Compiler warnings (cross-platform) ───────────────────────────────────────
target_compile_options(LoomicServer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
