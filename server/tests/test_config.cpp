#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

#include "LoomicServer/util/Config.hpp"

namespace {

// Writes a temporary JSON file and returns its path.
std::filesystem::path write_fixture(const std::string& name, const std::string& content)
{
    auto tmp = std::filesystem::temp_directory_path() / name;
    std::ofstream ofs(tmp);
    ofs << content;
    return tmp;
}

} // namespace

TEST(ConfigTest, LoadsValidFile)
{
    auto path = write_fixture("loomic_test_valid.json", R"({
        "bind_address": "127.0.0.1",
        "port": 9999,
        "http_health_port": 8181,
        "thread_count": 2,
        "pg_conn_string": "host=localhost dbname=test user=test",
        "jwt_secret": "test-secret",
        "log_level": "debug",
        "log_file": "",
        "metrics_port": 9191
    })");

    auto cfg = Loomic::Config::from_file(path);

    EXPECT_EQ(cfg.bind_address, "127.0.0.1");
    EXPECT_EQ(cfg.port, 9999);
    EXPECT_EQ(cfg.http_health_port, 8181);
    EXPECT_EQ(cfg.thread_count, 2u);
    EXPECT_EQ(cfg.pg_conn_string, "host=localhost dbname=test user=test");
    EXPECT_EQ(cfg.jwt_secret, "test-secret");
    EXPECT_EQ(cfg.log_level, "debug");
    EXPECT_EQ(cfg.metrics_port, 9191);
}

TEST(ConfigTest, ThrowsOnMissingJwtSecret)
{
    auto path = write_fixture("loomic_test_no_jwt.json", R"({
        "pg_conn_string": "host=localhost dbname=test user=test"
    })");

    EXPECT_THROW(Loomic::Config::from_file(path), std::runtime_error);
}

TEST(ConfigTest, ThrowsOnMissingPgConnString)
{
    auto path = write_fixture("loomic_test_no_pg.json", R"({
        "jwt_secret": "some-secret"
    })");

    EXPECT_THROW(Loomic::Config::from_file(path), std::runtime_error);
}

TEST(ConfigTest, ThrowsOnFileNotFound)
{
    EXPECT_THROW(
        Loomic::Config::from_file("/nonexistent/path/config.json"),
        std::runtime_error);
}

TEST(ConfigTest, DefaultValues)
{
    auto path = write_fixture("loomic_test_defaults.json", R"({
        "pg_conn_string": "host=localhost dbname=test user=test",
        "jwt_secret": "my-secret"
    })");

    auto cfg = Loomic::Config::from_file(path);

    EXPECT_EQ(cfg.bind_address, "0.0.0.0");
    EXPECT_EQ(cfg.port, 7777);
    EXPECT_EQ(cfg.http_health_port, 8080);
    EXPECT_EQ(cfg.thread_count, 0u);
    EXPECT_EQ(cfg.redis_host, "127.0.0.1");
    EXPECT_EQ(cfg.redis_port, 6379);
    EXPECT_EQ(cfg.metrics_port, 9090);
    EXPECT_EQ(cfg.log_level, "info");
}

TEST(ConfigTest, DefaultValues_Phase3Fields)
{
    // Phase 3 Cassandra / Redis defaults — no env vars, no file overrides.
    Loomic::Config cfg;

    EXPECT_EQ(cfg.cassandra_port,     10350);
    EXPECT_EQ(cfg.cassandra_keyspace, "loomic");
    EXPECT_EQ(cfg.cassandra_ssl,      true);
    EXPECT_EQ(cfg.redis_ssl,          false);
    EXPECT_EQ(cfg.redis_password,     "");
}

TEST(ConfigTest, EnvOverride_Phase3)
{
    // Set all Phase 3 env vars
    ::setenv("LOOMIC_CASSANDRA_CONTACT_POINTS", "cass.example.com",  1);
    ::setenv("LOOMIC_CASSANDRA_PORT",           "10350",             1);
    ::setenv("LOOMIC_CASSANDRA_USERNAME",       "testuser",          1);
    ::setenv("LOOMIC_CASSANDRA_PASSWORD",       "testpass",          1);
    ::setenv("LOOMIC_CASSANDRA_KEYSPACE",       "myspace",           1);
    ::setenv("LOOMIC_CASSANDRA_SSL",            "false",             1);
    ::setenv("LOOMIC_REDIS_PASSWORD",           "redispass",         1);
    ::setenv("LOOMIC_REDIS_SSL",                "true",              1);

    Loomic::Config cfg;
    Loomic::Config::from_env(cfg);

    EXPECT_EQ(cfg.cassandra_contact_points, "cass.example.com");
    EXPECT_EQ(cfg.cassandra_port,           10350);
    EXPECT_EQ(cfg.cassandra_username,       "testuser");
    EXPECT_EQ(cfg.cassandra_password,       "testpass");
    EXPECT_EQ(cfg.cassandra_keyspace,       "myspace");
    EXPECT_EQ(cfg.cassandra_ssl,            false);
    EXPECT_EQ(cfg.redis_password,           "redispass");
    EXPECT_EQ(cfg.redis_ssl,                true);

    // Clean up so other tests are not affected
    ::unsetenv("LOOMIC_CASSANDRA_CONTACT_POINTS");
    ::unsetenv("LOOMIC_CASSANDRA_PORT");
    ::unsetenv("LOOMIC_CASSANDRA_USERNAME");
    ::unsetenv("LOOMIC_CASSANDRA_PASSWORD");
    ::unsetenv("LOOMIC_CASSANDRA_KEYSPACE");
    ::unsetenv("LOOMIC_CASSANDRA_SSL");
    ::unsetenv("LOOMIC_REDIS_PASSWORD");
    ::unsetenv("LOOMIC_REDIS_SSL");
}
