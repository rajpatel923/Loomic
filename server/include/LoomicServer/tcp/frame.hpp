#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace Loomic {

namespace net = boost::asio;

using SslStream = net::ssl::stream<net::ip::tcp::socket>;

enum class MsgType : uint8_t {
    CHAT  = 0x01,
    AUTH  = 0x02,
    PING  = 0x03,
    PONG  = 0x04,
    ERROR = 0x05,
};

// 30-byte fixed header, little-endian.
// #pragma pack ensures no alignment padding between fields.
#pragma pack(push, 1)
struct FrameHeader {
    uint32_t payload_len;   // bytes 0–3
    uint8_t  msg_type;      // byte  4  (MsgType)
    uint8_t  flags;         // byte  5
    uint64_t msg_id;        // bytes 6–13
    uint64_t sender_id;     // bytes 14–21
    uint64_t recipient_id;  // bytes 22–29
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 30, "FrameHeader must be exactly 30 bytes");

// Awaitable frame I/O helpers (implemented in frame.cpp).
net::awaitable<FrameHeader>          read_frame_header(SslStream& s);
net::awaitable<std::vector<uint8_t>> read_frame_payload(SslStream& s, uint32_t len);
net::awaitable<void>                 write_frame(SslStream& s, const FrameHeader& hdr,
                                                  std::span<const uint8_t> payload);

} // namespace Loomic
