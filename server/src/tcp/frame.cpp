#include "LoomicServer/tcp/frame.hpp"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace Loomic {

namespace net = boost::asio;

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

} // namespace Loomic
