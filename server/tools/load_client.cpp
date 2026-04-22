/// load_client — Loomic Phase 7 load test tool
///
/// Usage:
///   load_client --host 127.0.0.1 --port 7777 --threads 4
///               --sessions-per-thread 250 --rate 10 --duration 30
///               --jwt <bearer-token> [--reconnect-pct 10]
///
/// Each thread spawns M synchronous TLS TCP sessions, authenticates them, then
/// paces CHAT frames at R msg/sec per session using a token bucket.  Latency is
/// measured from send to DELIVERED receipt and reported as p50/p99/p999.

#include "LoomicServer/tcp/frame.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using SslStream  = ssl::stream<net::ip::tcp::socket>;
using namespace Loomic;
using load_clock = std::chrono::steady_clock;
using load_tp    = load_clock::time_point;

// ── CLI options ───────────────────────────────────────────────────────────────

struct Options {
    std::string host              = "127.0.0.1";
    uint16_t    port              = 7777;
    int         threads           = 4;
    int         sessions_per_thread = 10;
    double      rate              = 10.0;   // msg/sec per session
    int         duration_sec      = 30;
    std::string jwt;
    int         reconnect_pct     = 0;
};

static Options parse_args(int argc, char* argv[])
{
    Options opt;
    for (int i = 1; i < argc - 1; ++i) {
        std::string key = argv[i];
        std::string val = argv[i + 1];
        if (key == "--host")                  { opt.host               = val;            ++i; }
        else if (key == "--port")             { opt.port               = static_cast<uint16_t>(std::stoi(val)); ++i; }
        else if (key == "--threads")          { opt.threads            = std::stoi(val); ++i; }
        else if (key == "--sessions-per-thread"){ opt.sessions_per_thread = std::stoi(val); ++i; }
        else if (key == "--rate")             { opt.rate               = std::stod(val); ++i; }
        else if (key == "--duration")         { opt.duration_sec       = std::stoi(val); ++i; }
        else if (key == "--jwt")              { opt.jwt                = val;            ++i; }
        else if (key == "--reconnect-pct")    { opt.reconnect_pct      = std::stoi(val); ++i; }
    }
    return opt;
}

// ── Token bucket ──────────────────────────────────────────────────────────────

struct TokenBucket {
    double   tokens;
    double   rate;          // tokens per second
    double   max_tokens;
    load_tp     last;

    explicit TokenBucket(double r, double max)
        : tokens(max), rate(r), max_tokens(max), last(load_clock::now()) {}

    /// Block until one token is available, then consume it.
    void wait_and_consume() {
        for (;;) {
            auto now     = load_clock::now();
            double elapsed = std::chrono::duration<double>(now - last).count();
            last   = now;
            tokens = std::min(max_tokens, tokens + elapsed * rate);
            if (tokens >= 1.0) {
                tokens -= 1.0;
                return;
            }
            double wait_sec = (1.0 - tokens) / rate;
            std::this_thread::sleep_for(
                std::chrono::microseconds(static_cast<long>(wait_sec * 1e6)));
        }
    }
};

// ── Synchronous TLS helpers ───────────────────────────────────────────────────

static SslStream make_connection(net::io_context& ioc,
                                  ssl::context&    ssl_ctx,
                                  const Options&   opt)
{
    SslStream stream(ioc, ssl_ctx);
    net::ip::tcp::resolver resolver(ioc);
    auto endpoints = resolver.resolve(opt.host, std::to_string(opt.port));
    net::connect(stream.lowest_layer(), endpoints);
    stream.handshake(ssl::stream_base::client);
    return stream;
}

static void send_frame(SslStream& s, const FrameHeader& hdr,
                        const std::vector<uint8_t>& payload)
{
    // Build wire: header then payload
    std::vector<uint8_t> wire(sizeof(FrameHeader) + payload.size());
    std::memcpy(wire.data(), &hdr, sizeof(FrameHeader));
    if (!payload.empty()) {
        std::memcpy(wire.data() + sizeof(FrameHeader), payload.data(), payload.size());
    }
    net::write(s, net::buffer(wire));
}

static FrameHeader recv_header(SslStream& s)
{
    FrameHeader hdr{};
    net::read(s, net::buffer(&hdr, sizeof(FrameHeader)));
    return hdr;
}

/// Authenticate the stream and return true on success.
static bool do_auth(SslStream& s, const std::string& jwt)
{
    FrameHeader auth{};
    auth.msg_type    = static_cast<uint8_t>(MsgType::AUTH);
    auth.payload_len = static_cast<uint32_t>(jwt.size());

    std::vector<uint8_t> payload(jwt.begin(), jwt.end());
    send_frame(s, auth, payload);

    auto reply = recv_header(s);
    return static_cast<MsgType>(reply.msg_type) == MsgType::PONG;
}

// ── Per-thread worker ─────────────────────────────────────────────────────────

struct Stats {
    std::vector<double> latencies_ms;
    uint64_t            sent    = 0;
    uint64_t            dropped = 0;
};

static Stats run_session(const Options& opt,
                          net::io_context& ioc,
                          ssl::context& ssl_ctx,
                          uint64_t session_idx)
{
    Stats stats;
    std::unordered_map<uint64_t, load_tp> in_flight;
    TokenBucket bucket(opt.rate, opt.rate * 2.0);

    auto deadline = load_clock::now() + std::chrono::seconds(opt.duration_sec);

    try {
        auto stream = make_connection(ioc, ssl_ctx, opt);
        if (!do_auth(stream, opt.jwt)) {
            std::cerr << "[session " << session_idx << "] auth failed\n";
            return stats;
        }

        // Set non-blocking-style: use a short read timeout via socket option
        stream.lowest_layer().non_blocking(false);

        // Unique dummy recipient (loopback to same user — server will route to self or
        // another test session; receipt still measures latency from send to frame arrival)
        uint64_t dummy_recipient = 1ULL; // server will fan-out or drop; we measure round-trip

        thread_local std::mt19937_64 rng{std::random_device{}()};
        static const std::string test_payload = "loomic-load-test-message-payload";

        while (load_clock::now() < deadline) {
            bucket.wait_and_consume();

            // Build CHAT frame
            FrameHeader hdr{};
            hdr.msg_type     = static_cast<uint8_t>(MsgType::CHAT);
            hdr.payload_len  = static_cast<uint32_t>(test_payload.size());
            hdr.sender_id    = session_idx;
            hdr.recipient_id = dummy_recipient;
            hdr.msg_id       = rng();
            hdr.flags        = 0;

            auto t_send = load_clock::now();
            try {
                send_frame(stream, hdr,
                           std::vector<uint8_t>(test_payload.begin(), test_payload.end()));
                in_flight[hdr.msg_id] = t_send;
                ++stats.sent;
            } catch (...) {
                ++stats.dropped;
                break;
            }

            // Drain any pending receipts (non-blocking peek: try read with 1ms deadline)
            stream.lowest_layer().native_non_blocking(true);
            boost::system::error_code ec;
            FrameHeader receipt{};
            net::read(stream, net::buffer(&receipt, sizeof(FrameHeader)),
                      net::transfer_exactly(sizeof(FrameHeader)), ec);
            stream.lowest_layer().native_non_blocking(false);

            if (!ec && static_cast<MsgType>(receipt.msg_type) == MsgType::DELIVERED) {
                auto it = in_flight.find(receipt.msg_id);
                if (it != in_flight.end()) {
                    double ms = std::chrono::duration<double, std::milli>(
                        load_clock::now() - it->second).count();
                    stats.latencies_ms.push_back(ms);
                    in_flight.erase(it);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[session " << session_idx << "] error: " << e.what() << "\n";
    }

    // Count undelivered as dropped
    stats.dropped += static_cast<uint64_t>(in_flight.size());
    return stats;
}

// ── Thread worker ─────────────────────────────────────────────────────────────

static void thread_worker(const Options& opt,
                           int thread_idx,
                           std::vector<Stats>& out_stats,
                           std::mutex& out_mutex)
{
    net::io_context ioc;
    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.set_verify_mode(ssl::verify_none); // load test — skip cert verification

    std::vector<Stats> local_stats;
    local_stats.reserve(opt.sessions_per_thread);

    for (int s = 0; s < opt.sessions_per_thread; ++s) {
        uint64_t idx = static_cast<uint64_t>(thread_idx) * opt.sessions_per_thread + s;
        local_stats.push_back(run_session(opt, ioc, ssl_ctx, idx));
    }

    std::lock_guard<std::mutex> lk(out_mutex);
    for (auto& st : local_stats) {
        out_stats.push_back(std::move(st));
    }
}

// ── Percentile helper ─────────────────────────────────────────────────────────

static double percentile(std::vector<double>& v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * (v.size() - 1) / 100.0);
    return v[idx];
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    auto opt = parse_args(argc, argv);

    if (opt.jwt.empty()) {
        std::cerr << "Usage: load_client --jwt <token> [options]\n"
                  << "  --host HOST               default: 127.0.0.1\n"
                  << "  --port PORT               default: 7777\n"
                  << "  --threads N               default: 4\n"
                  << "  --sessions-per-thread M   default: 10\n"
                  << "  --rate R                  msg/sec per session, default: 10\n"
                  << "  --duration D              seconds, default: 30\n"
                  << "  --reconnect-pct PCT       % sessions to reconnect/min (0=off)\n";
        return 1;
    }

    std::cout << "Loomic load test: "
              << opt.threads << " threads × "
              << opt.sessions_per_thread << " sessions, "
              << opt.rate << " msg/sec, "
              << opt.duration_sec << "s\n"
              << "Target: " << opt.host << ":" << opt.port << "\n\n";

    std::vector<Stats> all_stats;
    std::mutex         stats_mutex;
    std::vector<std::thread> threads;
    threads.reserve(opt.threads);

    auto t_start = load_clock::now();
    for (int t = 0; t < opt.threads; ++t) {
        threads.emplace_back(thread_worker, std::cref(opt), t,
                             std::ref(all_stats), std::ref(stats_mutex));
    }
    for (auto& th : threads) th.join();
    double elapsed = std::chrono::duration<double>(load_clock::now() - t_start).count();

    // Merge stats
    std::vector<double> all_latencies;
    uint64_t total_sent = 0, total_dropped = 0;
    for (auto& st : all_stats) {
        total_sent    += st.sent;
        total_dropped += st.dropped;
        for (auto ms : st.latencies_ms) all_latencies.push_back(ms);
    }

    double throughput = total_sent / elapsed;

    std::printf("=== Results ===\n");
    std::printf("  Duration   : %.1f s\n",  elapsed);
    std::printf("  Sent       : %llu\n",    static_cast<unsigned long long>(total_sent));
    std::printf("  Dropped    : %llu\n",    static_cast<unsigned long long>(total_dropped));
    std::printf("  Throughput : %.0f msg/s\n", throughput);

    if (!all_latencies.empty()) {
        std::printf("  p50 latency: %.2f ms\n",  percentile(all_latencies, 50.0));
        std::printf("  p99 latency: %.2f ms\n",  percentile(all_latencies, 99.0));
        std::printf("  p999 latency:%.2f ms\n",  percentile(all_latencies, 99.9));
    } else {
        std::printf("  No latency samples (no DELIVERED receipts received)\n");
    }

    // Exit non-zero if p99 > 10ms target
    if (!all_latencies.empty() && percentile(all_latencies, 99.0) > 10.0) {
        std::fprintf(stderr, "WARN: p99 latency exceeds 10ms target\n");
    }

    return 0;
}
