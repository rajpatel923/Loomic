#include "LoomicServer/tcp/frame.hpp"

#include "chat.pb.h"

#include <array>
#include <cstring>

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace Loomic {

namespace net = boost::asio;

namespace {

// LMS2 is the current wire format — includes a flags byte after msg_type.
// LMS1 is the legacy format (no flags byte) — decoded with flags=0 so that
// messages already sitting in the Redis offline queue remain deliverable.
constexpr std::array<std::uint8_t, 4> kDeliveryMagic  {'L', 'M', 'S', '2'};
constexpr std::array<std::uint8_t, 4> kDeliveryMagicV1{'L', 'M', 'S', '1'};

template <typename T>
void append_pod(std::vector<uint8_t>& out, const T& value)
{
    const auto* ptr = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), ptr, ptr + sizeof(T));
}

template <typename T>
bool read_pod(std::span<const uint8_t> data, std::size_t& offset, T& value)
{
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

std::optional<OutboundMessage> deserialize_legacy_frame(std::span<const uint8_t> payload)
{
    if (payload.size() < sizeof(FrameHeader)) {
        return std::nullopt;
    }

    FrameHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    if (payload.size() != sizeof(FrameHeader) + header.payload_len) {
        return std::nullopt;
    }

    loomic::ChatMessage proto_msg;
    if (!proto_msg.ParseFromArray(payload.data() + sizeof(FrameHeader),
                                  static_cast<int>(header.payload_len))) {
        return std::nullopt;
    }

    OutboundMessage message;
    message.conv_id = std::min(header.sender_id, header.recipient_id);
    message.msg_id = header.msg_id;
    message.sender_id = header.sender_id;
    message.recipient_id = header.recipient_id;
    message.timestamp_ms = proto_msg.timestamp_ms();
    message.msg_type = static_cast<MsgType>(header.msg_type);
    message.content.assign(proto_msg.content().begin(), proto_msg.content().end());
    return message;
}

} // namespace

net::awaitable<FrameHeader> read_frame_header(SslStream& s)
{
    FrameHeader hdr{};
    co_await net::async_read(s, net::buffer(&hdr, sizeof(hdr)), net::use_awaitable);
    co_return hdr;
}

net::awaitable<std::vector<uint8_t>> read_frame_payload(SslStream& s, uint32_t len)
{
    std::vector<uint8_t> buf(len);
    if (len > 0) {
        co_await net::async_read(s, net::buffer(buf), net::use_awaitable);
    }
    co_return buf;
}

net::awaitable<void> write_frame(SslStream& s, const FrameHeader& hdr,
                                  std::span<const uint8_t> payload)
{
    std::array<net::const_buffer, 2> bufs = {
        net::buffer(&hdr, sizeof(hdr)),
        net::buffer(payload.data(), payload.size())
    };
    co_await net::async_write(s, bufs, net::use_awaitable);
}

std::vector<uint8_t> serialize_delivery(const OutboundMessage& message)
{
    std::vector<uint8_t> out;
    out.reserve(4 + sizeof(message.conv_id) * 4 + sizeof(message.timestamp_ms)
                + sizeof(uint8_t) + sizeof(uint32_t) + message.content.size());

    out.insert(out.end(), kDeliveryMagic.begin(), kDeliveryMagic.end());
    append_pod(out, message.conv_id);
    append_pod(out, message.msg_id);
    append_pod(out, message.sender_id);
    append_pod(out, message.recipient_id);
    append_pod(out, message.timestamp_ms);

    const auto msg_type = static_cast<uint8_t>(message.msg_type);
    append_pod(out, msg_type);

    append_pod(out, message.flags);  // LMS2: flags byte after msg_type

    const auto payload_len = static_cast<uint32_t>(message.content.size());
    append_pod(out, payload_len);
    out.insert(out.end(), message.content.begin(), message.content.end());
    return out;
}

std::optional<OutboundMessage> deserialize_delivery(std::span<const uint8_t> payload)
{
    if (payload.size() < kDeliveryMagic.size()) {
        return deserialize_legacy_frame(payload);
    }

    // Check for current LMS2 magic
    if (std::equal(kDeliveryMagic.begin(), kDeliveryMagic.end(), payload.begin())) {
        std::size_t offset = kDeliveryMagic.size();
        OutboundMessage message;
        uint8_t msg_type   = 0;
        uint8_t flags      = 0;
        uint32_t payload_len = 0;

        if (!read_pod(payload, offset, message.conv_id)
            || !read_pod(payload, offset, message.msg_id)
            || !read_pod(payload, offset, message.sender_id)
            || !read_pod(payload, offset, message.recipient_id)
            || !read_pod(payload, offset, message.timestamp_ms)
            || !read_pod(payload, offset, msg_type)
            || !read_pod(payload, offset, flags)
            || !read_pod(payload, offset, payload_len)
            || offset + payload_len != payload.size()) {
            return std::nullopt;
        }

        message.msg_type = static_cast<MsgType>(msg_type);
        message.flags    = flags;
        message.content.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
        return message;
    }

    // Check for legacy LMS1 magic (no flags byte — flags defaults to 0)
    if (std::equal(kDeliveryMagicV1.begin(), kDeliveryMagicV1.end(), payload.begin())) {
        std::size_t offset = kDeliveryMagicV1.size();
        OutboundMessage message;
        uint8_t  msg_type    = 0;
        uint32_t payload_len = 0;

        if (!read_pod(payload, offset, message.conv_id)
            || !read_pod(payload, offset, message.msg_id)
            || !read_pod(payload, offset, message.sender_id)
            || !read_pod(payload, offset, message.recipient_id)
            || !read_pod(payload, offset, message.timestamp_ms)
            || !read_pod(payload, offset, msg_type)
            || !read_pod(payload, offset, payload_len)
            || offset + payload_len != payload.size()) {
            return std::nullopt;
        }

        message.msg_type = static_cast<MsgType>(msg_type);
        message.flags    = 0;
        message.content.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
        return message;
    }

    return deserialize_legacy_frame(payload);
}

std::vector<uint8_t> build_chat_frame(const OutboundMessage& message)
{
    loomic::ChatMessage proto_msg;
    proto_msg.set_msg_id(message.msg_id);
    proto_msg.set_sender_id(message.sender_id);
    proto_msg.set_recipient_id(message.recipient_id);
    proto_msg.set_content(message.content.data(), static_cast<int>(message.content.size()));
    proto_msg.set_timestamp_ms(message.timestamp_ms);
    proto_msg.set_type(loomic::CHAT);

    std::string proto_bytes = proto_msg.SerializeAsString();

    FrameHeader header{};
    header.payload_len  = static_cast<uint32_t>(proto_bytes.size());
    header.msg_type     = static_cast<uint8_t>(message.msg_type);
    header.flags        = message.flags;
    header.msg_id       = message.msg_id;
    header.sender_id    = message.sender_id;
    header.recipient_id = message.recipient_id;

    std::vector<uint8_t> frame(sizeof(FrameHeader) + proto_bytes.size());
    std::memcpy(frame.data(), &header, sizeof(header));
    std::memcpy(frame.data() + sizeof(header), proto_bytes.data(), proto_bytes.size());
    return frame;
}

} // namespace Loomic
