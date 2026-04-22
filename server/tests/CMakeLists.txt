find_package(GTest REQUIRED)

# ── test_config ───────────────────────────────────────────────────────────────
add_executable(test_config
    test_config.cpp
)

target_link_libraries(test_config
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_config COMMAND test_config)

# ── test_auth ─────────────────────────────────────────────────────────────────
add_executable(test_auth
    test_auth.cpp
)

target_link_libraries(test_auth
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_auth COMMAND test_auth)

# ── test_frame ─────────────────────────────────────────────────────────────────
add_executable(test_frame
    test_frame.cpp
)

target_link_libraries(test_frame
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_frame COMMAND test_frame)

# ── test_session ───────────────────────────────────────────────────────────────
add_executable(test_session
    test_session.cpp
)

target_link_libraries(test_session
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_session COMMAND test_session)

# ── test_groups ────────────────────────────────────────────────────────────────
add_executable(test_groups
    test_groups.cpp
)

target_link_libraries(test_groups
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_groups COMMAND test_groups)

# ── test_phase6 ────────────────────────────────────────────────────────────────
add_executable(test_phase6
    test_phase6.cpp
)

target_link_libraries(test_phase6
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_phase6 COMMAND test_phase6)

# ── test_hardening ─────────────────────────────────────────────────────────────
add_executable(test_hardening
    test_hardening.cpp
)

target_link_libraries(test_hardening
    PRIVATE
        LoomicServer_lib
        GTest::gtest_main
)

add_test(NAME test_hardening COMMAND test_hardening)
