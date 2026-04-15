#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>

#include "LoomicServer/tcp/frame.hpp"

namespace {

// ── Layout ────────────────────────────────────────────────────────────────────

TEST(FrameHeaderTest, FrameHeaderSize)
{
    EXPECT_EQ(sizeof(Loomic::FrameHeader), 30u);
}

TEST(FrameHeaderTest, FrameHeaderFieldOffsets)
{
    EXPECT_EQ(offsetof(Loomic::FrameHeader, payload_len),  0u);
    EXPECT_EQ(offsetof(Loomic::FrameHeader, msg_type),     4u);
    EXPECT_EQ(offsetof(Loomic::FrameHeader, flags),        5u);
    EXPECT_EQ(offsetof(Loomic::FrameHeader, msg_id),       6u);
    EXPECT_EQ(offsetof(Loomic::FrameHeader, sender_id),   14u);
    EXPECT_EQ(offsetof(Loomic::FrameHeader, recipient_id),22u);
}

// ── MsgType enum values ───────────────────────────────────────────────────────

TEST(FrameHeaderTest, MsgTypeValues)
{
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::CHAT),  0x01);
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::AUTH),  0x02);
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::PING),  0x03);
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::PONG),  0x04);
    EXPECT_EQ(static_cast<uint8_t>(Loomic::MsgType::ERROR), 0x05);
}

// ── Round-trip through a raw byte buffer ─────────────────────────────────────

TEST(FrameHeaderTest, FrameHeaderRoundTrip)
{
    Loomic::FrameHeader orig{};
    orig.payload_len  = 0xDEADBEEF;
    orig.msg_type     = static_cast<uint8_t>(Loomic::MsgType::CHAT);
    orig.flags        = 0x00;
    orig.msg_id       = 0x0102030405060708ULL;
    orig.sender_id    = 0xA1B2C3D4E5F60718ULL;
    orig.recipient_id = 0x1122334455667788ULL;

    uint8_t buf[30];
    std::memcpy(buf, &orig, sizeof(orig));

    Loomic::FrameHeader decoded{};
    std::memcpy(&decoded, buf, sizeof(decoded));

    EXPECT_EQ(decoded.payload_len,  orig.payload_len);
    EXPECT_EQ(decoded.msg_type,     orig.msg_type);
    EXPECT_EQ(decoded.flags,        orig.flags);
    EXPECT_EQ(decoded.msg_id,       orig.msg_id);
    EXPECT_EQ(decoded.sender_id,    orig.sender_id);
    EXPECT_EQ(decoded.recipient_id, orig.recipient_id);
}

} // namespace
