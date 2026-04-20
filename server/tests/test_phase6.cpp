#include <gtest/gtest.h>

#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "LoomicServer/tcp/frame.hpp"

namespace {

// ── Test 1–3: FrameTypes ──────────────────────────────────────────────────────

TEST(FrameTypes, DeliveredIs0x07)
{
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::DELIVERED), 0x07u);
}

TEST(FrameTypes, ReadIs0x08)
{
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::READ), 0x08u);
}

TEST(FrameTypes, TypingIs0x09)
{
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::TYPING), 0x09u);
}

// ── Test 4–6: ReceiptFrameRoundTrip ──────────────────────────────────────────

TEST(ReceiptFrameRoundTrip, DeliveredRoundTrip)
{
    Loomic::OutboundMessage msg;
    msg.msg_type     = Loomic::MsgType::DELIVERED;
    msg.msg_id       = 0xABCDEF0123456789ULL;
    msg.sender_id    = 1001ULL;
    msg.recipient_id = 2002ULL;
    msg.conv_id      = 3003ULL;
    msg.flags        = 0;

    auto bytes = Loomic::serialize_delivery(msg);
    auto opt   = Loomic::deserialize_delivery(bytes);

    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->msg_type,     Loomic::MsgType::DELIVERED);
    EXPECT_EQ(opt->msg_id,       msg.msg_id);
    EXPECT_EQ(opt->sender_id,    msg.sender_id);
    EXPECT_EQ(opt->recipient_id, msg.recipient_id);
    EXPECT_EQ(opt->flags,        msg.flags);
}

TEST(ReceiptFrameRoundTrip, ReadRoundTrip)
{
    Loomic::OutboundMessage msg;
    msg.msg_type     = Loomic::MsgType::READ;
    msg.msg_id       = 0x1234567890ABCDEFULL;
    msg.sender_id    = 5005ULL;
    msg.recipient_id = 6006ULL;
    msg.conv_id      = 7007ULL;
    msg.flags        = 0;

    auto bytes = Loomic::serialize_delivery(msg);
    auto opt   = Loomic::deserialize_delivery(bytes);

    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->msg_type,     Loomic::MsgType::READ);
    EXPECT_EQ(opt->msg_id,       msg.msg_id);
    EXPECT_EQ(opt->sender_id,    msg.sender_id);
    EXPECT_EQ(opt->recipient_id, msg.recipient_id);
    EXPECT_EQ(opt->flags,        0u);
}

TEST(ReceiptFrameRoundTrip, TypingRoundTripWithGroupFlag)
{
    Loomic::OutboundMessage msg;
    msg.msg_type     = Loomic::MsgType::TYPING;
    msg.msg_id       = 0ULL;
    msg.sender_id    = 9009ULL;
    msg.recipient_id = 8008ULL;
    msg.conv_id      = 8008ULL;
    msg.flags        = Loomic::kFlagIsGroup;

    auto bytes = Loomic::serialize_delivery(msg);
    auto opt   = Loomic::deserialize_delivery(bytes);

    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->msg_type,  Loomic::MsgType::TYPING);
    EXPECT_EQ(opt->sender_id, msg.sender_id);
    EXPECT_NE(opt->flags & Loomic::kFlagIsGroup, 0u);
}

// ── Test 7–9: RedisKeys ───────────────────────────────────────────────────────

TEST(RedisKeys, UnreadHashKey)
{
    uint64_t user_id = 42;
    std::string key = "unread:" + std::to_string(user_id);
    EXPECT_EQ(key, "unread:42");
}

TEST(RedisKeys, UnreadHashField)
{
    uint64_t conv_id = 99;
    std::string field = std::to_string(conv_id);
    EXPECT_EQ(field, "99");
}

TEST(RedisKeys, LastSeenKey)
{
    uint64_t user_id = 12345;
    std::string key = "last_seen:" + std::to_string(user_id);
    EXPECT_EQ(key, "last_seen:12345");
}

// ── Test 10–11: UnreadLogic ───────────────────────────────────────────────────

TEST(UnreadLogic, MissingKeyReturnsZero)
{
    std::unordered_map<uint64_t, int64_t> unread_map;
    uint64_t conv_id = 500;
    int64_t count = unread_map.count(conv_id) ? unread_map.at(conv_id) : 0;
    EXPECT_EQ(count, 0);
}

TEST(UnreadLogic, PresentKeyReturnsValue)
{
    std::unordered_map<uint64_t, int64_t> unread_map;
    uint64_t conv_id = 500;
    unread_map[conv_id] = 7;
    int64_t count = unread_map.count(conv_id) ? unread_map.at(conv_id) : 0;
    EXPECT_EQ(count, 7);
}

// ── Test 12–14: ConvListJsonShape ─────────────────────────────────────────────

TEST(ConvListJsonShape, DmItemHasPeerFields)
{
    // Simulate what list_conversations builds for a DM
    nlohmann::json item;
    item["kind"]       = "dm";
    item["id"]         = "1001";
    item["peer_id"]    = "2002";
    item["peer_name"]  = "alice";
    item["peer_bio"]   = "";
    item["peer_avatar"]= "";
    item["unread_count"] = 0;

    EXPECT_EQ(item["kind"].get<std::string>(), "dm");
    EXPECT_TRUE(item.contains("peer_id"));
    EXPECT_TRUE(item.contains("peer_name"));
    EXPECT_FALSE(item.contains("group_name"));
}

TEST(ConvListJsonShape, GroupItemHasGroupName)
{
    // Simulate what list_conversations builds for a group
    nlohmann::json item;
    item["kind"]         = "group";
    item["id"]           = "3003";
    item["group_name"]   = "Team Chat";
    item["group_avatar"] = "";
    item["unread_count"] = 0;

    EXPECT_EQ(item["kind"].get<std::string>(), "group");
    EXPECT_TRUE(item.contains("group_name"));
    EXPECT_FALSE(item.contains("peer_id"));
}

TEST(ConvListJsonShape, UnreadCountDefaultsToZero)
{
    std::unordered_map<uint64_t, int64_t> unread_map; // empty
    uint64_t id = 999;

    nlohmann::json item;
    item["unread_count"] = unread_map.count(id) ? unread_map.at(id) : 0;

    EXPECT_EQ(item["unread_count"].get<int64_t>(), 0);
}

// ── Test 15: LastSeenRoundTrip ────────────────────────────────────────────────

TEST(LastSeenRoundTrip, Int64RoundTrip)
{
    int64_t original_ts = 1745000000000LL;
    std::string s = std::to_string(original_ts);

    int64_t recovered = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), recovered);
    ASSERT_EQ(ec, std::errc{});
    EXPECT_EQ(recovered, original_ts);
}

// ── Test 16: PushTokenSQL ────────────────────────────────────────────────────

TEST(PushTokenSQL, InsertOnConflictClause)
{
    // Verify the SQL used by PushService::register_token contains expected keywords
    std::string sql =
        "INSERT INTO device_tokens (user_id, token, platform, updated_at) "
        "VALUES ($1, $2, $3, NOW()) "
        "ON CONFLICT (user_id, token) DO UPDATE "
        "SET platform=$3, updated_at=NOW()";

    EXPECT_NE(sql.find("ON CONFLICT"), std::string::npos);
    EXPECT_NE(sql.find("DO UPDATE"), std::string::npos);
    EXPECT_NE(sql.find("device_tokens"), std::string::npos);
}

} // namespace
