vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO apache/cassandra-cpp-driver
    REF "2.17.1"
    SHA512 1675d3fe2d4ddf5ec4a68629e94a0381b860626e0377db1dffe9208641db181dff08c170ccf5c43a10b021e2899cb08da6502235b8437318001f1c5a4023d976
    HEAD_REF master
)

# Patch the driver's CMakeLists.txt for compatibility issues.
set(_cmake "${SOURCE_PATH}/CMakeLists.txt")
file(READ "${_cmake}" _contents)

# cmake 4.0+ requires cmake_minimum_required >= 3.5; cassandra uses 2.8.12.
string(REPLACE
    [[cmake_minimum_required(VERSION 2.8.12)]]
    [[cmake_minimum_required(VERSION 3.5)]]
    _contents "${_contents}")

# The driver does not handle AppleClang and fatals out.
# Replace the three Clang-only checks to also accept AppleClang.

string(REPLACE
    [[elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")]]
    [[elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")]]
    _contents "${_contents}")

string(REPLACE
    [[if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
   "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")]]
    [[if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
   "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang" OR
   "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")]]
    _contents "${_contents}")

string(REPLACE
    [[if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") ]]
    [[if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")]]
    _contents "${_contents}")

# CMake 4 no longer tolerates this project generating headers into the source tree
# during an out-of-source configure. Generate them into the binary tree instead
# and add the corresponding build include directories.
file(WRITE "${_cmake}" "${_contents}")

set(_src_cmake "${SOURCE_PATH}/src/CMakeLists.txt")
file(READ "${_src_cmake}" _src_contents)

string(REPLACE
    [[add_subdirectory(third_party/sparsehash)]]
    [[add_subdirectory(third_party/sparsehash)

list(APPEND INCLUDE_DIRS
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/third_party/sparsehash/src)]]
    _src_contents "${_src_contents}")

string(REPLACE
    [[${CMAKE_CURRENT_SOURCE_DIR}/driver_config.hpp]]
    [[${CMAKE_CURRENT_BINARY_DIR}/driver_config.hpp]]
    _src_contents "${_src_contents}")

file(WRITE "${_src_cmake}" "${_src_contents}")

set(_sparsehash_cmake "${SOURCE_PATH}/src/third_party/sparsehash/CMakeLists.txt")
file(READ "${_sparsehash_cmake}" _sparsehash_contents)

string(REPLACE
    [[configure_file("config.h.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/src/sparsehash/internal/sparseconfig.h")]]
    [[file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/sparsehash/internal")
configure_file("config.h.cmake" "${CMAKE_CURRENT_BINARY_DIR}/src/sparsehash/internal/sparseconfig.h")]]
    _sparsehash_contents "${_sparsehash_contents}")

file(WRITE "${_sparsehash_cmake}" "${_sparsehash_contents}")

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" CASS_BUILD_SHARED)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static"  CASS_BUILD_STATIC)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DLIBUV_ROOT_DIR=${CURRENT_INSTALLED_DIR}
        -DCASS_BUILD_SHARED=${CASS_BUILD_SHARED}
        -DCASS_BUILD_STATIC=${CASS_BUILD_STATIC}
        -DCASS_BUILD_EXAMPLES=OFF
        -DCASS_BUILD_TESTS=OFF
        -DCASS_BUILD_INTEGRATION_TESTS=OFF
        -DCASS_BUILD_UNIT_TESTS=OFF
        -DCASS_USE_OPENSSL=ON
        -DCASS_USE_ZLIB=ON
        -DCASS_USE_TIMERFD=ON
        -DCASS_USE_KERBEROS=OFF
        -DCASS_USE_LIBSSH2=OFF
        -DCASS_USE_BOOST_ATOMIC=OFF
        -DCASS_INSTALL_HEADER=ON
        -DCASS_INSTALL_HEADER_IN_SUBDIR=OFF
        -DCASS_INSTALL_PKG_CONFIG=ON
        -DCASS_MULTICORE_COMPILATION=ON
)

vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    foreach(_pc_file
        "${CURRENT_PACKAGES_DIR}/lib/pkgconfig/cassandra_static.pc"
        "${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/cassandra_static.pc"
    )
        if(EXISTS "${_pc_file}")
            # vcpkg's static libuv port exports libuv-static.pc, not libuv.pc.
            vcpkg_replace_string("${_pc_file}" "Requires: libuv" "Requires: libuv-static")
        endif()
    endforeach()
endif()

vcpkg_fixup_pkgconfig()

# The driver does not generate a CMake config-file package.
# Write one so that find_package(cassandra) and the cassandra::cassandra
# target work the same way as every other vcpkg dependency.
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    set(CASS_LIB_NAMES "cassandra_static")
else()
    set(CASS_LIB_NAMES "cassandra")
endif()

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/cassandra")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/cassandra/cassandra-config.cmake" "
get_filename_component(_CASS_ROOT \"\${CMAKE_CURRENT_LIST_DIR}/../..\" ABSOLUTE)

find_library(CASSANDRA_LIBRARY
    NAMES ${CASS_LIB_NAMES}
    PATHS \"\${_CASS_ROOT}/lib\"
    NO_DEFAULT_PATH
    REQUIRED
)

if(NOT TARGET cassandra::cassandra)
    add_library(cassandra::cassandra UNKNOWN IMPORTED)
    set_target_properties(cassandra::cassandra PROPERTIES
        IMPORTED_LOCATION \"\${CASSANDRA_LIBRARY}\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${_CASS_ROOT}/include\"
    )
endif()

set(cassandra_FOUND TRUE)
set(cassandra_VERSION \"2.17.1\")
")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
