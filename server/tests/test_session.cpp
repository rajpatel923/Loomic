#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include "LoomicServer/tcp/Session.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"

namespace {

// Helper: construct a Session with a disconnected socket and null service ptrs.
// The registry never calls any Session methods, so null service ptrs are safe.
std::shared_ptr<Loomic::Session> make_session()
{
    static boost::asio::io_context  ioc;
    static boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tls_server);
    return std::make_shared<Loomic::Session>(
        Loomic::SslStream(ioc.get_executor(), ssl_ctx),
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, std::string{});
}

// ── TokenBucket ───────────────────────────────────────────────────────────────

TEST(TokenBucketTest, FirstConsumeSucceeds)
{
    Loomic::TokenBucket tb;
    EXPECT_TRUE(tb.consume());
}

TEST(TokenBucketTest, BurstOfTwenty)
{
    Loomic::TokenBucket tb;
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(tb.consume()) << "consume #" << (i + 1) << " should succeed";
    }
}

TEST(TokenBucketTest, TwentyFirstFails)
{
    Loomic::TokenBucket tb;
    for (int i = 0; i < 20; ++i) tb.consume();
    // Elapsed time ≈ 0 ms → negligible refill (<<1 token at 5/s)
    EXPECT_FALSE(tb.consume());
}

TEST(TokenBucketTest, RefillsAfterDelay)
{
    Loomic::TokenBucket tb;
    // Drain the bucket
    for (int i = 0; i < 20; ++i) tb.consume();

    // Rewind last_refill by 10 seconds → would refill 50 tokens, capped at 20
    tb.last_refill -= std::chrono::seconds(10);

    EXPECT_TRUE(tb.consume());
}

TEST(TokenBucketTest, PartialRefill)
{
    Loomic::TokenBucket tb;
    // Drain the bucket
    for (int i = 0; i < 20; ++i) tb.consume();

    // Rewind by 2 seconds → 2 * 5 = 10 new tokens
    tb.last_refill -= std::chrono::seconds(2);

    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(tb.consume()) << "consume #" << (i + 1) << " should succeed";
    }
    EXPECT_FALSE(tb.consume()) << "11th consume should fail (no tokens left)";
}

// ── SessionRegistry ───────────────────────────────────────────────────────────

TEST(SessionRegistryTest, InsertAndLookup)
{
    Loomic::SessionRegistry reg;
    auto sess = make_session();
    reg.insert(1, sess);
    EXPECT_NE(reg.lookup(1), nullptr);
}

TEST(SessionRegistryTest, LookupMissing)
{
    Loomic::SessionRegistry reg;
    EXPECT_EQ(reg.lookup(42), nullptr);
}

TEST(SessionRegistryTest, Remove)
{
    Loomic::SessionRegistry reg;
    auto sess = make_session();
    reg.insert(2, sess);
    reg.remove(2);
    EXPECT_EQ(reg.lookup(2), nullptr);
}

TEST(SessionRegistryTest, OverwriteInsert)
{
    Loomic::SessionRegistry reg;
    auto sessA = make_session();
    auto sessB = make_session();
    reg.insert(3, sessA);
    reg.insert(3, sessB);
    EXPECT_EQ(reg.lookup(3), sessB);
}

TEST(SessionRegistryTest, ExpiredWeakPtr)
{
    Loomic::SessionRegistry reg;
    {
        auto sess = make_session();
        reg.insert(99, sess);
        // sess destroyed here; weak_ptr in registry expires
    }
    EXPECT_EQ(reg.lookup(99), nullptr);
}

TEST(SessionRegistryTest, ConcurrentInsertLookup)
{
    Loomic::SessionRegistry reg;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&reg, &success_count, i]() {
            auto sess = make_session();
            uint64_t uid = static_cast<uint64_t>(i + 100);
            reg.insert(uid, sess);
            if (reg.lookup(uid) != nullptr) {
                ++success_count;
            }
        });
    }
    for (auto& t : threads) t.join();

    EXPECT_EQ(success_count.load(), 4);
}

} // namespace
