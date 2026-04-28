#include "server/RenderServer.h"

#include "io/SceneLoader.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaRenderer.h"
    #include "gpu/GpuScene.h"
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#ifndef _WIN32
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace rr::server {

namespace {

// --- Line / token helpers ------------------------------------------------
//
// The dispatcher receives a single request line. Trim trailing CR
// (Windows clients send CRLF), then strip leading / trailing ASCII
// whitespace so the command parsing is forgiving without being
// surprising.

std::string_view trim(std::string_view s) {
    auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Split `line` into `(verb, args)`. `verb` is the first
// whitespace-delimited token (lower-cased for case-insensitive
// matching); `args` is whatever follows the first run of
// separators, trimmed of leading whitespace. Trailing whitespace
// in `args` is preserved for commands like `load_scene` whose
// argument is a path that may legitimately contain trailing
// characters - the path is passed to the filesystem as-is.
struct Parsed {
    std::string      verb;
    std::string_view args;
};

Parsed split_verb(std::string_view line) {
    Parsed p;
    auto pos = line.find_first_of(" \t");
    std::string_view verb = (pos == std::string_view::npos) ? line
                                                            : line.substr(0, pos);
    p.verb.assign(verb);
    std::transform(p.verb.begin(), p.verb.end(), p.verb.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (pos == std::string_view::npos) {
        p.args = std::string_view{};
    } else {
        auto rest = line.substr(pos);
        // Skip the run of whitespace separating verb from args.
        std::size_t i = 0;
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
        p.args = rest.substr(i);
    }
    return p;
}

// --- Per-command handlers ------------------------------------------------

CommandResult cmd_ping() {
    return CommandResult{ /*response=*/"OK pong", /*wants_shutdown=*/false };
}

CommandResult cmd_shutdown() {
    return CommandResult{ /*response=*/"OK goodbye", /*wants_shutdown=*/true };
}

CommandResult cmd_load_scene(std::string_view path_arg, ServerState& state) {
    if (path_arg.empty()) {
        return { "ERR load_scene requires a path argument", false };
    }
    const std::filesystem::path path{std::string(path_arg)};
    auto load = rr::io::load_rrscene(path);
    if (!load.ok) {
        return { "ERR load_scene failed: " + load.message, false };
    }

    state.scene           = std::move(load.scene);
    state.scene_loaded    = true;
    state.last_scene_path = path;

    std::ostringstream os;
    os << "OK loaded "
       << state.scene.materials.size() << " materials, "
       << state.scene.spheres.size()   << " spheres, "
       << state.scene.lights.size()    << " lights, "
       << state.scene.meshes.size()    << " meshes";
    return { os.str(), false };
}

CommandResult cmd_set_beta(std::string_view value_arg, ServerState& state) {
    if (value_arg.empty()) {
        return { "ERR set_beta requires a numeric argument", false };
    }
    // strtof tolerates trailing whitespace; reject anything that
    // doesn't consume a real number.
    std::string buf(value_arg);
    char* end = nullptr;
    const float beta = std::strtof(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return { "ERR set_beta: not a number: " + buf, false };
    }
    if (!std::isfinite(beta)) {
        return { "ERR set_beta: not finite", false };
    }
    // The relativity stack already clamps |beta| < 1 at use time
    // (`clampBeta`), but reject values that would make the math
    // degenerate so the client gets immediate feedback.
    if (beta <= -1.0f || beta >= 1.0f) {
        return { "ERR set_beta: |value| must be < 1", false };
    }

    // v1 convention: the scalar `beta` sets the observer's velocity
    // along +x. Multi-axis velocities are reachable through the
    // scene file; the server protocol stays one-knob to match the
    // command spec.
    state.scene.observer.velocity = rr::math::Vec3{beta, 0.0f, 0.0f};

    std::ostringstream os;
    os << "OK beta set to " << beta;
    return { os.str(), false };
}

CommandResult cmd_render(ServerState& state) {
    if (!state.scene_loaded) {
        return { "ERR render: no scene loaded (call load_scene first)", false };
    }

#ifdef RR_HAS_CUDA
    rr::gpu::GpuScene gpu_scene;
    if (!gpu_scene.upload_from(state.scene)) {
        return { "ERR render: GPU upload failed (no CUDA device or "
                 "device allocation refused)", false };
    }

    const int width  = state.scene.render_settings.width;
    const int height = state.scene.render_settings.height;
    auto result = rr::cuda::CudaRenderer::render_scene(gpu_scene, width, height);
    if (!result.ok) {
        return { "ERR render: " + result.message, false };
    }

    std::error_code ec;
    if (state.output_path.has_parent_path()) {
        std::filesystem::create_directories(state.output_path.parent_path(), ec);
    }
    if (!result.image.save_ppm(state.output_path)) {
        return { "ERR render: saving image failed: " + state.output_path.string(),
                 false };
    }
    return { "OK rendered to " + state.output_path.string(), false };
#else
    (void)state;
    return { "ERR render: no CUDA backend compiled in "
             "(rebuild with -DRR_ENABLE_CUDA=ON)", false };
#endif
}

}  // namespace

CommandResult dispatch_command(std::string_view line, ServerState& state) {
    const auto trimmed = trim(line);
    if (trimmed.empty()) {
        return { "ERR empty command", false };
    }
    const auto parsed = split_verb(trimmed);
    const auto args   = trim(parsed.args);

    if (parsed.verb == "ping")        return cmd_ping();
    if (parsed.verb == "shutdown")    return cmd_shutdown();
    if (parsed.verb == "load_scene")  return cmd_load_scene(args, state);
    if (parsed.verb == "set_beta")    return cmd_set_beta(args, state);
    if (parsed.verb == "render")      return cmd_render(state);

    return { "ERR unknown command: " + parsed.verb, false };
}

// --- TCP server ----------------------------------------------------------

RenderServer::RenderServer(ServerConfig cfg) : config_(std::move(cfg)) {}

#ifdef _WIN32

// Windows support is a follow-up. The renderer server is primarily
// developed on Linux + macOS where the BSD socket API is the same
// header set; the Windows path needs WinSock initialisation
// (`WSAStartup`) and `closesocket` instead of `close`. The stub
// keeps the build green on Windows by failing loudly at runtime.
RenderServer::RunResult RenderServer::run() {
    return { false, "RenderServer is not implemented on Windows yet "
                    "(BSD-socket port pending)" };
}

#else

namespace {

// One iovec-style helper: drain the kernel's send buffer so the
// reply is fully written before the next recv. `send` may write
// fewer bytes than requested under load.
bool send_all(int fd, const char* data, std::size_t len) {
    while (len > 0) {
        const auto n = ::send(fd, data, len, /*flags=*/0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        data += n;
        len  -= static_cast<std::size_t>(n);
    }
    return true;
}

bool send_response(int fd, const std::string& body) {
    // Body may already contain trailing newlines per the
    // dispatcher's choice; we always append our own '\n' after
    // the body and the END terminator so the wire format stays
    // strict. Empty bodies still get a status line - dispatcher
    // never returns an empty response.
    if (!send_all(fd, body.data(), body.size())) return false;
    static constexpr char kTerminator[] = "\nEND\n";
    return send_all(fd, kTerminator, sizeof(kTerminator) - 1);
}

// Drain whole lines from `recv` into `inbox`. Returns true while
// the connection is still open; false on EOF / error.
bool read_more(int fd, std::string& inbox) {
    char buf[1024];
    const auto n = ::recv(fd, buf, sizeof(buf), /*flags=*/0);
    if (n == 0) return false;       // peer closed cleanly
    if (n < 0) {
        if (errno == EINTR) return true;
        return false;
    }
    inbox.append(buf, static_cast<std::size_t>(n));
    return true;
}

// Pop one line (ending in '\n') from `inbox` if present. The
// terminator is stripped from the returned string. Returns true
// iff a line was extracted.
bool pop_line(std::string& inbox, std::string& out) {
    const auto pos = inbox.find('\n');
    if (pos == std::string::npos) return false;
    out.assign(inbox, 0, pos);
    inbox.erase(0, pos + 1);
    return true;
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

}  // namespace

RenderServer::RunResult RenderServer::run() {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, /*protocol=*/0);
    if (listen_fd < 0) {
        return { false, std::string("socket() failed: ") + std::strerror(errno) };
    }

    // SO_REUSEADDR so a quick restart doesn't trip TIME_WAIT.
    int yes = 1;
    if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                     &yes, sizeof(yes)) < 0) {
        const auto msg = std::string("setsockopt(SO_REUSEADDR) failed: ")
                       + std::strerror(errno);
        ::close(listen_fd);
        return { false, msg };
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<std::uint16_t>(config_.port));
    if (::inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
        ::close(listen_fd);
        return { false, "invalid host: " + config_.host };
    }

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const auto msg = std::string("bind(") + config_.host + ":"
                       + std::to_string(config_.port) + ") failed: "
                       + std::strerror(errno);
        ::close(listen_fd);
        return { false, msg };
    }

    if (::listen(listen_fd, /*backlog=*/1) < 0) {
        const auto msg = std::string("listen() failed: ") + std::strerror(errno);
        ::close(listen_fd);
        return { false, msg };
    }

    std::clog << "[server] listening on " << config_.host << ":"
              << config_.port << std::endl;

    bool shutdown_requested = false;
    while (!shutdown_requested) {
        sockaddr_in peer{};
        socklen_t   peer_len = sizeof(peer);
        int client_fd = ::accept(listen_fd,
                                 reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            std::clog << "[server] accept() failed: " << std::strerror(errno)
                      << std::endl;
            continue;
        }

        char ip_buf[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip_buf, sizeof(ip_buf));
        std::clog << "[server] client connected: " << ip_buf << std::endl;

        std::string inbox;
        bool keep_alive = true;
        while (keep_alive) {
            std::string line;
            while (!pop_line(inbox, line)) {
                if (!read_more(client_fd, inbox)) {
                    keep_alive = false;
                    break;
                }
            }
            if (!keep_alive) break;

            const auto result = dispatch_command(line, state_);
            if (!send_response(client_fd, result.response)) {
                keep_alive = false;
                break;
            }
            if (result.wants_shutdown) {
                shutdown_requested = true;
                keep_alive         = false;
            }
        }

        close_fd(client_fd);
        std::clog << "[server] client disconnected" << std::endl;
    }

    close_fd(listen_fd);
    std::clog << "[server] stopped" << std::endl;
    return { true, "" };
}

#endif  // _WIN32

}  // namespace rr::server
