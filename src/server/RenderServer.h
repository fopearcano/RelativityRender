#pragma once

#include <cstddef>
#include <string>

namespace rr::server {

// Stage 15A.1 renderer-server skeleton. Pure-POSIX TCP server
// (`socket(2)` / `bind(2)` / `listen(2)` / `accept(2)`); this
// initial slice is Linux-only. A future cross-platform sub-
// stage can pull the platform code into a backend layer when
// Windows support is needed.
//
// Protocol (newline-delimited ASCII, per-connection one-shot):
//
//   client -> server : `<command>\n`
//   server -> client : `<response>\n`
//   server closes connection
//
// Stage 15A.1 supports exactly one command:
//
//   `ping` -> `pong`
//
// Any other command yields `error: unknown command`. A
// command that exceeds the read buffer (256 bytes) yields
// `error: command too long`. Read or write failures yield
// `error: io error` and close the connection. The server
// serves one client at a time (single-threaded, blocking
// `accept`); concurrent clients arrive on the next
// `serve_one()` cycle.
//
// No render command, no upload of geometry / textures / AOVs,
// no shutdown of the renderer. Subsequent 15A+ sub-stages add
// the protocol commands that drive a render dispatch and
// stream results back.
class RenderServer {
public:
    struct Config {
        // Bind address. Stage 15A.1's "localhost only" rule
        // hardcodes the default to the loopback interface;
        // callers can override but should not unless they
        // know what they're doing (the server has no auth /
        // sandboxing / rate-limiting yet).
        std::string bind_address = "127.0.0.1";

        // TCP port. Default 7777 per the Stage 15A.1 prompt.
        int         port         = 7777;
    };

    // Single-cycle outcome of `serve_one()`. `ok` is true iff
    // the server successfully accepted a client, read a
    // command, sent a response, and closed the connection.
    // Per-cycle errors (read failure, write failure, oversized
    // command) populate `error_message`; the listen socket
    // stays open so the next `serve_one()` can try again.
    struct ServeResult {
        bool        ok = false;
        std::string command;        // received text, trimmed of trailing \r\n
        std::string response;       // text sent back, no trailing \n
        std::string client_address; // dotted-decimal source IP
        int         client_port = 0;
        std::string error_message;  // populated on per-cycle failure
    };

    RenderServer() = default;
    explicit RenderServer(Config config);
    ~RenderServer();

    // Move-only: owns an OS file descriptor.
    RenderServer(const RenderServer&)            = delete;
    RenderServer& operator=(const RenderServer&) = delete;
    RenderServer(RenderServer&&) noexcept;
    RenderServer& operator=(RenderServer&&) noexcept;

    // Open the listen socket: socket / setsockopt(SO_REUSEADDR)
    // / bind / listen. Returns true on success; on failure
    // populates `last_error()` with the reason and leaves the
    // server in its pre-start state. Calling `start()` on an
    // already-listening server is a no-op success.
    [[nodiscard]] bool start();

    // Close the listen socket if it is open. Safe to call
    // repeatedly and on a moved-from / never-started server.
    // The destructor invokes this automatically.
    void stop() noexcept;

    // Accept one client, read one command, respond, close. The
    // result struct describes the cycle's outcome; the server
    // remains listening regardless of per-cycle outcome (so
    // the caller can loop). Returns
    // `{ok = false, error_message = "not started"}` if the
    // server has not been started.
    [[nodiscard]] ServeResult serve_one();

    [[nodiscard]] bool        is_listening()  const noexcept { return listen_fd_ >= 0; }
    [[nodiscard]] int         port()          const noexcept { return config_.port; }
    [[nodiscard]] const std::string& bind_address() const noexcept {
        return config_.bind_address;
    }

    // Reason of the most recent `start()` failure. Empty when
    // the server is currently listening or has never been
    // started.
    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }

    // Maximum length, in bytes, of a single command line
    // including the trailing newline. Commands longer than
    // this are rejected with `error: command too long`.
    static constexpr std::size_t kMaxCommandBytes = 256;

private:
    Config      config_{};
    int         listen_fd_ = -1;
    std::string last_error_;
};

}  // namespace rr::server
