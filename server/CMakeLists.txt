cmake_minimum_required(VERSION 3.25)
#cmake_policy(SET CMP0167 NEW)

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
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Output directories ────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── vcpkg packages ────────────────────────────────────────────────────────────
find_package(Boost          REQUIRED COMPONENTS system)
find_package(spdlog         REQUIRED)
find_package(GTest          REQUIRED)
find_package(nlohmann_json  REQUIRED)
find_package(OpenSSL        REQUIRED)
find_package(hiredis        REQUIRED)
find_package(jwt-cpp        REQUIRED)
find_package(prometheus-cpp REQUIRED)
find_package(libpqxx        REQUIRED)


# ── LoomicServer_lib (static) ─────────────────────────────────────────────────
add_library(LoomicServer_lib STATIC
    src/util/Config.cpp
    src/util/Logger.cpp
    src/core/Server.cpp
    src/http/HttpServer.cpp
    src/http/AuthHandler.cpp
    src/http/DocsHandler.cpp
    src/tcp/TcpServer.cpp
    src/db/PgPool.cpp
    src/middleware/RateLimiter.cpp
    src/auth/SnowflakeGen.cpp
    src/auth/JwtService.cpp
    src/auth/PasswordService.cpp
)

target_include_directories(LoomicServer_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(LoomicServer_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -O2>
)

# Workaround: clang-cl rejects [[gnu::noinline]] / [[gnu::cold]] placed after
# __declspec(dllimport) (the PQXX_LIBEXPORT expansion).  Pre-defining
# PQXX_LIBEXPORT as empty bypasses the guard in pqxx/internal/header-pre.hxx
# so the GNU attributes land in front of the return type — a valid C++ position.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32)
    target_compile_definitions(LoomicServer_lib PRIVATE PQXX_LIBEXPORT=)
endif()

target_link_libraries(LoomicServer_lib
    PUBLIC
        Boost::headers
        Boost::system
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
        libpqxx::pqxx
        jwt-cpp::jwt-cpp
)

# ── LoomicServer (executable) ─────────────────────────────────────────────────
add_executable(LoomicServer
    src/main.cpp
        src/http/HealthHandler.cpp
)

target_link_libraries(LoomicServer
    PRIVATE
        LoomicServer_lib
)

# ── Proto integration ─────────────────────────────────────────────────────────
# TODO: wire protobuf code generation here when proto sources become part of
# the build.

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
