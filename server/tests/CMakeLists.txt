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
