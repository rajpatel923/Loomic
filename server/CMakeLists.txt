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

# ── vcpkg packages ────────────────────────────────────────────────────────────
find_package(Boost          REQUIRED COMPONENTS system)
find_package(protobuf       QUIET)  # Phase 2 — not linked yet
find_package(spdlog         REQUIRED)
find_package(GTest          REQUIRED)
find_package(absl           REQUIRED)
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

target_link_libraries(LoomicServer_lib
    PUBLIC
        Boost::headers
        Boost::system
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
        absl::flat_hash_map
        absl::strings
        libpqxx::pqxx
        jwt-cpp::jwt-cpp
)

# ── LoomicServer (executable) ─────────────────────────────────────────────────
add_executable(LoomicServer
    src/main.cpp
)

target_link_libraries(LoomicServer
    PRIVATE
        LoomicServer_lib
)

# ── Protobuf code-gen stub (Phase 2) ──────────────────────────────────────────
# find_package(protobuf REQUIRED) is already called above.
# Proto file generation will be wired here in Phase 2.

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
