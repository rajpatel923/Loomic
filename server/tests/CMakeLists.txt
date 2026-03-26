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
