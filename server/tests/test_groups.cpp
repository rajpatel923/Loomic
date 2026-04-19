#include <gtest/gtest.h>

#include "LoomicServer/tcp/frame.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"

#include <charconv>
#include <string>
#include <system_error>

using namespace Loomic;

// ── 1. IS_GROUP flag value ────────────────────────────────────────────────────

TEST(Groups, IsFlagIsGroupValue) {
    EXPECT_EQ(kFlagIsGroup, 0x01u);
}

TEST(Groups, IsFlagIsGroupBitDetection) {
    uint8_t flags_with    = 0x01;
    uint8_t flags_without = 0x00;
    uint8_t flags_other   = 0x02;

    EXPECT_TRUE (flags_with    & kFlagIsGroup);
    EXPECT_FALSE(flags_without & kFlagIsGroup);
    EXPECT_FALSE(flags_other   & kFlagIsGroup);

    // Bit 0 set alongside other bits
    EXPECT_TRUE(static_cast<uint8_t>(0x03u) & kFlagIsGroup);
}

// ── 2. DELETE_NOTIFY enum value ───────────────────────────────────────────────

TEST(Groups, DeleteNotifyMsgTypeValue) {
    EXPECT_EQ(static_cast<uint8_t>(MsgType::DELETE_NOTIFY), 0x06u);

    // Ensure other values are unchanged
    EXPECT_EQ(static_cast<uint8_t>(MsgType::CHAT),  0x01u);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::AUTH),  0x02u);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::PING),  0x03u);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::PONG),  0x04u);
    EXPECT_EQ(static_cast<uint8_t>(MsgType::ERROR), 0x05u);
}

// ── 3. Redis key construction ─────────────────────────────────────────────────

TEST(Groups, GroupRedisKeyFormat) {
    uint64_t group_id = 12345678901234567ULL;
    std::string key = "group:" + std::to_string(group_id) + ":members";
    EXPECT_EQ(key, "group:12345678901234567:members");
}

TEST(Groups, PresenceRedisKeyFormat) {
    uint64_t user_id = 9876543210ULL;
    std::string key = "presence:" + std::to_string(user_id);
    EXPECT_EQ(key, "presence:9876543210");
}

TEST(Groups, OfflineQueueRedisKeyFormat) {
    uint64_t user_id = 1000000000000000000ULL;
    std::string key = "offline:" + std::to_string(user_id);
    EXPECT_EQ(key, "offline:1000000000000000000");
}

// ── 4. SessionRegistry fan-out mock ──────────────────────────────────────────

TEST(Groups, RegistryMissReturnsNullptr) {
    SessionRegistry reg;
    EXPECT_EQ(reg.lookup(111), nullptr);
    EXPECT_EQ(reg.lookup(222), nullptr);
    EXPECT_EQ(reg.lookup(0),   nullptr);
}

TEST(Groups, RegistryRemoveNonExistentIsNoop) {
    SessionRegistry reg;
    // Should not throw
    reg.remove(999);
    EXPECT_EQ(reg.lookup(999), nullptr);
}

// ── 5. ID string parsing ──────────────────────────────────────────────────────

TEST(Groups, ValidSnowflakeIdParsing) {
    std::string s = "7816523049238528";
    uint64_t id  = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), id);
    EXPECT_EQ(ec, std::errc{});
    EXPECT_EQ(id, 7816523049238528ULL);
}

TEST(Groups, InvalidIdStringRejected) {
    std::string s = "not_a_number";
    uint64_t id  = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), id);
    EXPECT_NE(ec, std::errc{});
}

TEST(Groups, EmptyIdStringRejected) {
    std::string s;
    uint64_t id  = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), id);
    EXPECT_NE(ec, std::errc{});
}

TEST(Groups, MaxUint64Parsing) {
    std::string s = "18446744073709551615";
    uint64_t id  = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), id);
    EXPECT_EQ(ec, std::errc{});
    EXPECT_EQ(id, UINT64_MAX);
}

// ── 6. OutboundMessage flags field ────────────────────────────────────────────

TEST(Groups, OutboundMessageFlagsDefaultZero) {
    OutboundMessage msg;
    EXPECT_EQ(msg.flags, 0u);
}

TEST(Groups, OutboundMessageFlagsGroupBit) {
    OutboundMessage msg;
    msg.flags = kFlagIsGroup;
    EXPECT_TRUE(msg.flags & kFlagIsGroup);
    EXPECT_EQ(msg.flags, kFlagIsGroup);
}

TEST(Groups, OutboundMessageFlagsIsPartOfStruct) {
    // Compile-time check: flags field exists and is uint8_t
    static_assert(std::is_same_v<decltype(OutboundMessage::flags), uint8_t>,
                  "OutboundMessage::flags must be uint8_t");
}

TEST(Groups, OutboundMessageRoundTripFlags) {
    OutboundMessage msg;
    msg.conv_id      = 42ULL;
    msg.msg_id       = 100ULL;
    msg.sender_id    = 1ULL;
    msg.recipient_id = 2ULL;
    msg.timestamp_ms = 1704067200000LL;
    msg.msg_type     = MsgType::CHAT;
    msg.flags        = kFlagIsGroup;
    msg.content      = {0x68, 0x69};  // "hi"

    auto bytes = serialize_delivery(msg);
    auto decoded = deserialize_delivery(bytes);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->flags, kFlagIsGroup);
    EXPECT_EQ(decoded->conv_id, 42ULL);
    EXPECT_EQ(decoded->msg_type, MsgType::CHAT);
    EXPECT_EQ(decoded->content.size(), 2u);
}
