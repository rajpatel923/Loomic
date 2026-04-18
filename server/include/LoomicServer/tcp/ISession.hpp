#pragma once

#include <cstdint>
#include <vector>

#include <boost/asio/strand.hpp>
#include <boost/asio/any_io_executor.hpp>

namespace Loomic {

/// Abstract interface shared by Session (TLS TCP) and WebSocketSession.
/// SessionRegistry holds weak_ptr<ISession> so both session types
/// can be looked up and messaged uniformly.
class ISession {
public:
    virtual ~ISession() = default;

    /// Enqueue a binary frame (FrameHeader + proto payload) for delivery.
    /// Implementations may re-serialize to a different wire format (e.g. JSON
    /// for WebSocket sessions).  Must be called on the session's strand.
    virtual void enqueue(std::vector<uint8_t> frame) = 0;

    /// Returns the strand that serialises all operations on this session.
    virtual boost::asio::strand<boost::asio::any_io_executor>& strand() = 0;
};

} // namespace Loomic
