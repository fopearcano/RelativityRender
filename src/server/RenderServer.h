#pragma once

#include "scene/Scene.h"

#include <cstddef>
#include <optional>
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
// Stage 15A.1 shipped exactly one command (`ping` -> `pong`).
// Stage 15B.1 adds:
//
//   `load_scene <path>` -> `ok: scene loaded ...` (summary)
//                       -> `error: scene load failed: <msg>`
//
// Arguments are split off the verb at the first whitespace
// character (space or tab). Paths with embedded whitespace are
// not supported in this minimum-viable wire format; a future
// sub-stage may grow the parser to handle quoted arguments.
//
// On a successful `load_scene`, the parsed scene is stored on
// the server (see `loaded_scene()`) for subsequent commands to
// consume. An empty / failing load leaves the previously-loaded
// scene (if any) untouched - "atomic" semantics matching the
// rest of the project's `upload_*` paths.
//
// Any other command yields `error: unknown command`. A
// command that exceeds the read buffer (256 bytes) yields
// `error: command too long`. Read or write failures yield
// `error: io error` and close the connection. The server
// serves one client at a time (single-threaded, blocking
// `accept`); concurrent clients arrive on the next
// `serve_one()` cycle.
//
// No render command, no upload of geometry / textures / AOVs to
// the GPU, no shutdown of the renderer. Subsequent 15B+ sub-
// stages add the protocol commands that drive a render dispatch
// and stream results back.
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

    // Raw OS file descriptor of the listen socket, or -1 when the
    // server is not started. Stage 15A.2 exposes this so a CLI
    // signal handler (`SIGINT` / `SIGTERM`) can wake a blocked
    // `accept()` via the async-signal-safe call
    // `::shutdown(fd, SHUT_RDWR)`. This accessor is the only
    // sanctioned way for non-`RenderServer` code to touch the
    // underlying fd; the caller must NOT close it directly (that
    // is `stop()`'s job) and must not call other `RenderServer`
    // methods from within the signal handler.
    [[nodiscard]] int         listen_fd()     const noexcept { return listen_fd_; }

    // Reason of the most recent `start()` failure. Empty when
    // the server is currently listening or has never been
    // started.
    [[nodiscard]] const std::string& last_error() const noexcept {
        return last_error_;
    }

    // Stage 15B.1: the most recently loaded scene. `std::nullopt`
    // means "no scene loaded yet" or "every load attempt so far
    // has failed". A successful `load_scene <path>` command
    // overwrites the slot atomically; a failing load leaves the
    // previously-loaded value (if any) intact, matching the
    // "no partial state" precedent the project's `upload_*`
    // paths set. The server does not yet act on the loaded
    // scene - subsequent 15B+ sub-stages add the render command
    // that consumes it.
    [[nodiscard]] const std::optional<rr::scene::Scene>& loaded_scene() const noexcept {
        return loaded_scene_;
    }

    // Maximum length, in bytes, of a single command line
    // including the trailing newline. Commands longer than
    // this are rejected with `error: command too long`.
    static constexpr std::size_t kMaxCommandBytes = 256;

private:
    // Translate a single client command line (already trimmed
    // of trailing `\r\n`) into the response text the server
    // sends back (without the trailing `\n`). Mutates the
    // server's stateful slots when a command modifies them
    // (currently only `load_scene` updates `loaded_scene_`).
    [[nodiscard]] std::string handle_command(const std::string& command);

    Config                            config_{};
    int                               listen_fd_ = -1;
    std::string                       last_error_;
    std::optional<rr::scene::Scene>   loaded_scene_;
};

}  // namespace rr::server
