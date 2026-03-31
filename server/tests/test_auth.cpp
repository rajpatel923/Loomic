#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/auth/JwtService.hpp"

using namespace Loomic;

// ── SnowflakeGen ──────────────────────────────────────────────────────────────

TEST(SnowflakeGen, GeneratesPositiveIds)
{
    SnowflakeGen gen(1);
    EXPECT_GT(gen.next(), 0);
}

TEST(SnowflakeGen, SequentialIdsAreMonotonicallyIncreasing)
{
    SnowflakeGen gen(1);
    auto a = gen.next();
    auto b = gen.next();
    EXPECT_LT(a, b);
}

TEST(SnowflakeGen, IdsAreUniqueAcrossThreads)
{
    SnowflakeGen gen(1);
    std::vector<int64_t> ids;
    std::mutex m;

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < 50; ++j) {
                auto id = gen.next();
                std::lock_guard<std::mutex> lk(m);
                ids.push_back(id);
            }
        });
    }
    for (auto& t : threads) t.join();

    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::unique(ids.begin(), ids.end()), ids.end())
        << "Duplicate IDs found across threads";
}

// ── JwtService ────────────────────────────────────────────────────────────────

TEST(JwtService, IssueAndVerifyRoundTrip)
{
    JwtService jwt("super-secret-key-for-tests");
    auto token = jwt.issue(42, std::chrono::hours(1));
    ASSERT_FALSE(token.empty());

    auto user = jwt.verify(token);
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->uid, 42);
}

TEST(JwtService, RejectsGarbage)
{
    JwtService jwt("super-secret-key-for-tests");
    auto user = jwt.verify("this.is.not.a.jwt");
    EXPECT_FALSE(user.has_value());
}

TEST(JwtService, RejectsWrongSecret)
{
    JwtService signer("secret-A");
    JwtService verifier("secret-B");
    auto token = signer.issue(99, std::chrono::hours(1));
    EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST(JwtService, LargeUidPreserved)
{
    JwtService jwt("some-secret");
    int64_t big_uid = 1234567890123456789LL;
    auto token = jwt.issue(big_uid, std::chrono::hours(1));
    auto user  = jwt.verify(token);
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->uid, big_uid);
}
