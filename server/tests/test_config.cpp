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
