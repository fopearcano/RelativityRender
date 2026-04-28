#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <string>
#include <string_view>

// Renderer server foundation (M18 / Module 19).
//
// Long-running TCP service the renderer exposes to external clients
// (the future Cinema 4D bridge, a CLI submitter, an interactive
// preview UI). v1 protocol is intentionally minimal:
//
//   - Plain ASCII, line-based.
//   - One client at a time. The accept loop is serial: one client
//     keeps the connection open, sends commands, and closes (or
//     issues `shutdown`); the server returns to accept after
//     each session. Multi-client / threaded handling lands later.
//   - Five v1 commands: ping, load_scene <path>, render,
//     set_beta <value>, shutdown.
//   - Each request is one line terminated by '\n'. The server
//     replies with one or more lines and ends the response with a
//     terminator line `END\n`. The first reply line starts with
//     either `OK ` (success) or `ERR ` (failure) so a client can
//     parse the status without having to know each command's
//     reply shape.
//
// The server does not link UI or the Cinema 4D bridge - the
// dependency direction is one-way: the bridge is a client of this
// server, not the other way around. Per `docs/MODULE_MAP.md`,
// the server module sits in the renderer-platform layer (L6).
//
// The render command is a no-op on builds without CUDA: it
// reports `ERR no CUDA backend compiled in`. The full render path
// runs end-to-end when `-DRR_ENABLE_CUDA=ON`.

namespace rr::server {

struct ServerConfig {
    // Bind address. The default keeps the server local-only;
    // exposing it on `0.0.0.0` is an explicit opt-in by the
    // caller.
    std::string host = "127.0.0.1";

    // TCP port. The v1 default is 7777 to keep the protocol on
    // a fixed, easy-to-remember port for the bridge.
    int port = 7777;
};

// Mutable state the server holds across commands within a single
// run. `dispatch_command` mutates this directly; the server does
// not expose it to clients beyond the effects of each command.
struct ServerState {
    bool                  scene_loaded   = false;
    rr::scene::Scene      scene;
    std::filesystem::path last_scene_path;

    // Output path for the render command. Fixed for v1 so the
    // server contract stays simple; later versions will let the
    // client choose per-render.
    std::filesystem::path output_path = "output/server_render.ppm";

    // Bookkeeping for the most recent successful render. Populated
    // only on a successful `render` dispatch; left at the defaults
    // when no render has succeeded yet. Useful both for the OK
    // reply (resolution + absolute path) and for clients that may
    // poll status in a future protocol slice.
    int                   render_count       = 0;
    int                   last_render_width  = 0;
    int                   last_render_height = 0;
    std::filesystem::path last_render_path;
};

// Result of dispatching one request line. The dispatcher is pure
// (no IO); the server's TCP loop turns this into bytes on the
// wire. `wants_shutdown` is set only by the `shutdown` command and
// instructs the loop to close the connection and exit
// `RenderServer::run`.
struct CommandResult {
    std::string response;            // body, NOT including the trailing END line
    bool        wants_shutdown = false;
};

// Parse one request line, mutate `state`, and return the reply.
// The line must be the request body only (no trailing '\n'); the
// dispatcher trims surrounding whitespace and tolerates a CR
// before the implicit newline so a Windows client (CRLF
// terminator) interoperates.
//
// Unknown commands produce `ERR unknown command: <token>`. Empty
// or whitespace-only lines reply with `ERR empty command`. The
// rendered output of `render` is saved to `state.output_path`;
// the response carries the saved path so a client can fetch it.
[[nodiscard]] CommandResult dispatch_command(std::string_view line,
                                             ServerState&     state);

// TCP server. Construct, then call `run()` to enter the accept
// loop on the calling thread. `run()` returns when the
// `shutdown` command closes the listening socket, or when the
// server fails to bind / listen.
class RenderServer {
public:
    RenderServer() = default;
    explicit RenderServer(ServerConfig cfg);

    struct RunResult {
        bool        ok = false;
        std::string message;       // populated only on early failure
    };

    // Block on the accept loop. Each accepted connection is
    // handled to completion before the next is accepted (one
    // client at a time per the v1 spec). Returns `ok = true`
    // when a clean `shutdown` command terminates the loop;
    // returns `ok = false` with a descriptive message when the
    // listening socket cannot be created / bound.
    [[nodiscard]] RunResult run();

    [[nodiscard]] const ServerConfig& config() const noexcept { return config_; }
    [[nodiscard]] const ServerState&  state()  const noexcept { return state_;  }

private:
    ServerConfig config_{};
    ServerState  state_{};
};

}
