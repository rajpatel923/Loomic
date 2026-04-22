#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <regex>
#include <set>
#include <thread>
#include <vector>

#include "LoomicServer/metrics/MetricsRegistry.hpp"
#include "LoomicServer/middleware/RateLimiter.hpp"
#include "LoomicServer/util/RequestContext.hpp"
#include "LoomicServer/util/Uuid.hpp"

using namespace Loomic;

// ── UuidTest (3) ───────────────────────────────────────────────────────────────

TEST(UuidTest, Format) {
    const auto id = generate_uuid_v4();
    // RFC 4122 version 4 format: xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx
    static const std::regex uuid_re(
        "[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");
    EXPECT_TRUE(std::regex_match(id, uuid_re)) << "UUID does not match RFC 4122 v4: " << id;
}

TEST(UuidTest, Uniqueness) {
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) {
        ids.insert(generate_uuid_v4());
    }
    EXPECT_EQ(ids.size(), 1000u) << "Duplicate UUIDs generated";
}

TEST(UuidTest, ThreadSafety) {
    std::set<std::string> all_ids;
    std::mutex            set_mutex;
    std::vector<std::thread> threads;
    threads.reserve(8);

    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 100; ++i) {
                auto id = generate_uuid_v4();
                std::lock_guard<std::mutex> lk(set_mutex);
                all_ids.insert(id);
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(all_ids.size(), 800u) << "Duplicate UUIDs detected across threads";
}

// ── RequestContextTest (2) ─────────────────────────────────────────────────────

TEST(RequestContextTest, DefaultEmpty) {
    // Spawn a fresh thread to get a default-constructed thread_local.
    std::string val = "not-empty";
    std::thread([&]() { val = g_request_ctx.request_id; }).join();
    EXPECT_TRUE(val.empty()) << "Default request_id should be empty";
}

TEST(RequestContextTest, SetAndRead) {
    g_request_ctx.request_id = "test-request-id-42";
    EXPECT_EQ(g_request_ctx.request_id, "test-request-id-42");
    g_request_ctx.request_id.clear();
}

// ── MetricsRegistryTest (4) ────────────────────────────────────────────────────
// Uses a high ephemeral port (19091) to avoid conflicts with the running server.

class MetricsRegistryTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // init() is idempotent (std::call_once); safe to call once per test binary.
        MetricsRegistry::init(19091);
    }
};

TEST_F(MetricsRegistryTest, SingletonStable) {
    auto* a = &MetricsRegistry::get();
    auto* b = &MetricsRegistry::get();
    EXPECT_EQ(a, b) << "get() must return the same singleton instance";
}

TEST_F(MetricsRegistryTest, CounterIncrements) {
    auto& counter = MetricsRegistry::get().messages_total();
    const double before = counter.Value();
    counter.Increment();
    EXPECT_DOUBLE_EQ(counter.Value(), before + 1.0);
}

TEST_F(MetricsRegistryTest, GaugeIncDec) {
    auto& gauge = MetricsRegistry::get().active_sessions();
    const double baseline = gauge.Value();
    gauge.Increment();
    gauge.Decrement();
    EXPECT_DOUBLE_EQ(gauge.Value(), baseline);
}

TEST_F(MetricsRegistryTest, HistogramNoThrow) {
    EXPECT_NO_THROW(MetricsRegistry::get().message_latency_ms().Observe(5.0));
}

// ── RateLimiterTest (5) ────────────────────────────────────────────────────────

TEST(RateLimiterTest, FirstRequestAllowed) {
    RateLimiter rl(5.0, 1.0);
    EXPECT_TRUE(rl.allow("user1"));
}

TEST(RateLimiterTest, ExhaustBurst) {
    // max_tokens=20, slow refill — burst of 20 succeeds, 21st fails.
    RateLimiter rl(20.0, 0.001);
    for (int i = 0; i < 20; ++i) {
        EXPECT_TRUE(rl.allow("key")) << "Expected true on call " << (i + 1);
    }
    EXPECT_FALSE(rl.allow("key")) << "21st call must be rejected";
}

TEST(RateLimiterTest, DifferentKeysIndependent) {
    RateLimiter rl(1.0, 0.0);  // 1 token, no refill
    EXPECT_TRUE(rl.allow("keyA"));
    EXPECT_FALSE(rl.allow("keyA"));  // keyA exhausted
    EXPECT_TRUE(rl.allow("keyB"));   // keyB has its own fresh bucket
}

TEST(RateLimiterTest, RefillAfterElapsed) {
    // 2 tokens, fast refill (1000/sec) — exhaust then sleep 2ms to refill.
    RateLimiter rl(2.0, 1000.0);
    EXPECT_TRUE(rl.allow("key"));
    EXPECT_TRUE(rl.allow("key"));
    EXPECT_FALSE(rl.allow("key"));  // exhausted

    std::this_thread::sleep_for(std::chrono::milliseconds(3));  // ~3 tokens refilled
    EXPECT_TRUE(rl.allow("key")) << "Bucket should have refilled after sleep";
}

TEST(RateLimiterTest, MaxTokensCapped) {
    // Verify tokens don't exceed max_tokens after a long sleep.
    RateLimiter rl(2.0, 10000.0);  // burst 2, very fast refill
    EXPECT_TRUE(rl.allow("k"));   // init: tokens = max-1 = 1, consume → true
    EXPECT_TRUE(rl.allow("k"));   // consume last → true
    EXPECT_FALSE(rl.allow("k"));  // empty → false

    // Sleep long enough that uncapped refill would give >2 tokens.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // theoretical +100 tokens

    // Capped at 2: exactly 2 more successful calls, then failure.
    EXPECT_TRUE(rl.allow("k"));
    EXPECT_TRUE(rl.allow("k"));
    EXPECT_FALSE(rl.allow("k")) << "Tokens should be capped at max_tokens=2";
}

// ── SanitizerSmokeTest (2) ─────────────────────────────────────────────────────
// These tests always pass; they exist so ASan/TSan jobs exercise the binary.

TEST(SanitizerSmokeTest, NoDataRace) {
    std::atomic<int> counter{0};
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 1000; ++j) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(counter.load(), 4000);
}

TEST(SanitizerSmokeTest, NoBoundsViolation) {
    std::vector<int> v = {1, 2, 3, 4, 5};
    int sum = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
    }
    EXPECT_EQ(sum, 15);
}
