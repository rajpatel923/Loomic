cmake_minimum_required(VERSION 3.25)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

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

# Apple clang searches /usr/local/include before the SDK. Prefer the manifest-local
# vcpkg headers so Homebrew installs of absl/utf8-range cannot override them.
set(LOOMIC_VCPKG_INCLUDE_DIR "")
if(DEFINED VCPKG_TARGET_TRIPLET AND EXISTS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include")
    set(LOOMIC_VCPKG_INCLUDE_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include")
endif()

# ── vcpkg packages ────────────────────────────────────────────────────────────
find_package(Boost          REQUIRED COMPONENTS system)
find_package(spdlog         REQUIRED)
find_package(GTest          REQUIRED)
find_package(nlohmann_json  REQUIRED)
find_package(OpenSSL        REQUIRED)
find_package(ZLIB           REQUIRED)
find_package(libuv CONFIG   REQUIRED)
find_package(hiredis        REQUIRED)
find_package(hiredis_ssl    REQUIRED)
find_package(jwt-cpp        REQUIRED)
find_package(prometheus-cpp REQUIRED)
find_package(libpqxx        REQUIRED)
find_package(Protobuf       REQUIRED)
find_package(absl           REQUIRED)
find_package(cassandra      REQUIRED)

set(LOOMIC_HAS_CASSANDRA ON)

set(LOOMIC_LIBUV_TARGET "")
if(TARGET libuv::uv)
    set(LOOMIC_LIBUV_TARGET libuv::uv)
elseif(TARGET libuv::uv_a)
    set(LOOMIC_LIBUV_TARGET libuv::uv_a)
else()
    message(FATAL_ERROR "libuv target not found after find_package(libuv)")
endif()

# ── Protobuf code generation ──────────────────────────────────────────────────
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/chat.proto)

# ── LoomicServer_lib (static) ─────────────────────────────────────────────────
add_library(LoomicServer_lib STATIC
    src/util/Config.cpp
    src/util/Logger.cpp
    src/core/Server.cpp
    src/http/HttpServer.cpp
    src/http/AuthHandler.cpp
    src/http/DocsHandler.cpp
    src/http/ConversationsHandler.cpp
    src/http/GroupsHandler.cpp
    src/http/MessagesHandler.cpp
    src/http/UsersHandler.cpp
    src/http/ReceiptsHandler.cpp
    src/push/PushService.cpp
    src/push/PushHandler.cpp
    src/tcp/TcpServer.cpp
    src/ws/WebSocketSession.cpp
    src/tcp/Session.cpp
    src/tcp/SessionRegistry.cpp
    src/tcp/frame.cpp
    src/db/PgPool.cpp
    src/db/RedisClient.cpp
    src/db/CassandraClient.cpp
    src/middleware/RateLimiter.cpp
    src/auth/SnowflakeGen.cpp
    src/auth/JwtService.cpp
    src/auth/PasswordService.cpp
    ${PROTO_SRCS}
)

target_include_directories(LoomicServer_lib BEFORE
    PUBLIC
        ${LOOMIC_VCPKG_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}   # generated proto headers (chat.pb.h)
)

target_compile_options(LoomicServer_lib PRIVATE
    $<$<BOOL:${LOOMIC_VCPKG_INCLUDE_DIR}>:-I${LOOMIC_VCPKG_INCLUDE_DIR}>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -O2>
)

target_compile_definitions(LoomicServer_lib PRIVATE
    $<$<BOOL:${LOOMIC_HAS_CASSANDRA}>:LOOMIC_HAS_CASSANDRA=1>
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
        hiredis::hiredis
        hiredis::hiredis_ssl
        protobuf::libprotobuf
        absl::flat_hash_map
)

if(LOOMIC_HAS_CASSANDRA)
    target_link_libraries(LoomicServer_lib
            PUBLIC
            cassandra::cassandra
            ${LOOMIC_LIBUV_TARGET}
            ZLIB::ZLIB
    )
endif()

# ── LoomicServer (executable) ─────────────────────────────────────────────────
add_executable(LoomicServer
    src/main.cpp
        src/http/HealthHandler.cpp
)

target_link_libraries(LoomicServer
    PRIVATE
        LoomicServer_lib
)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
