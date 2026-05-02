#include "server/RenderServer.h"

#include "io/SceneLoader.h"
#include "relativity/RelativityMath.h"  // clampBeta (existing utility)
#include "server/SocketPlatform.h"

#include <array>
#include <cmath>     // std::sqrt, std::isnan, std::isinf
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

// Stage-15-render: the wire `render` command needs the same GPU
// orchestration `--render-from-scene` does in main.cpp. We pull
// the headers in only when CUDA is compiled in - on a no-CUDA
// build the render branch returns a "requires CUDA" error and
// touches none of these symbols.
#ifdef RR_HAS_CUDA
    #include "cuda/CudaRenderer.h"
    #include "geometry/Sphere.h"
    #include "gpu/GpuScene.h"
    #include "image/Image.h"
    #include "lighting/Light.h"
    #include "material/MaterialTypes.h"

    #include <filesystem>
    #include <system_error>
    #include <vector>
#endif

namespace rr::server {

namespace {

// Read from `fd` into a single-line buffer until the first
// newline, EOF, or `max_bytes` total bytes (whichever comes
// first). Strips a trailing `\r` if present. Returns:
//   {ok = true,  line = ...} on success (newline OR EOF after
//                              some bytes).
//   {ok = false, line = "", error = "io error"} on read error.
//   {ok = false, line = "", error = "command too long"} when
//                              `max_bytes` is exhausted with no
//                              newline.
//   {ok = false, line = "", error = "empty command"} on
//                              zero-byte read (client closed).
struct ReadLineResult {
    bool        ok = false;
    std::string line;
    std::string error;
};

ReadLineResult read_line(socket_t fd, std::size_t max_bytes) {
    std::string acc;
    acc.reserve(64);
    std::array<char, 64> buf{};

    while (acc.size() < max_bytes) {
        const std::size_t want =
            std::min(buf.size(), max_bytes - acc.size());
        // recv is `int` on Windows + `ssize_t` on POSIX; both
        // sign-extend SOCKET_ERROR / -1 to negative when stored
        // in a signed int.
        const auto n = ::recv(fd, buf.data(),
                              static_cast<int>(want), 0);
        if (n < 0) {
            if (socketWasInterrupted()) continue;
            return {false, {}, "io error: " + lastSocketErrorMessage()};
        }
        if (n == 0) {
            if (acc.empty()) {
                return {false, {}, "empty command"};
            }
            // EOF after partial content: treat as a valid line.
            break;
        }
        const std::size_t before = acc.size();
        acc.append(buf.data(), static_cast<std::size_t>(n));
        // Search for newline only inside the new bytes.
        const auto newline_pos = acc.find('\n', before);
        if (newline_pos != std::string::npos) {
            acc.resize(newline_pos);
            break;
        }
    }

    if (acc.size() >= max_bytes
     && acc.find('\n') == std::string::npos) {
        return {false, {}, "command too long"};
    }

    if (!acc.empty() && acc.back() == '\r') {
        acc.pop_back();
    }
    return {true, std::move(acc), {}};
}

// Send the full payload, retrying on EINTR + short writes.
bool write_all(socket_t fd, const std::string& payload) {
    const char* p = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const auto n = ::send(fd, p,
                              static_cast<int>(remaining), 0);
        if (n < 0) {
            if (socketWasInterrupted()) continue;
            return false;
        }
        if (n == 0) {
            return false;  // peer closed
        }
        p         += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

// Split a single command line into a verb (everything before the
// first whitespace) and an argument tail (everything after, with
// leading whitespace trimmed). A line with no whitespace yields
// `{line, ""}`. Trailing whitespace on the tail is preserved (a
// path with significant trailing spaces is the caller's
// problem; today this never matters).
struct ParsedCommand {
    std::string verb;
    std::string args;
};

ParsedCommand parse_command_line(const std::string& line) {
    const auto sep = line.find_first_of(" \t");
    if (sep == std::string::npos) {
        return {line, ""};
    }
    ParsedCommand p;
    p.verb = line.substr(0, sep);
    // Skip every contiguous whitespace character between verb
    // and args; the wire format is whitespace-tolerant.
    auto first_arg = line.find_first_not_of(" \t", sep);
    if (first_arg != std::string::npos) {
        p.args = line.substr(first_arg);
    }
    return p;
}

// Parse a single float from `s`. On success returns `true` and
// writes the parsed value into `*out`; rejects empty strings,
// strings with trailing non-whitespace junk, and inf / NaN
// values. Used by `set_beta` to validate its scalar argument
// before it reaches `rr::relativity::clampBeta`.
bool parse_finite_float(const std::string& s, float* out) {
    if (s.empty() || out == nullptr) {
        return false;
    }
    try {
        std::size_t pos = 0;
        const float v = std::stof(s, &pos);
        for (std::size_t i = pos; i < s.size(); ++i) {
            if (s[i] != ' ' && s[i] != '\t') return false;
        }
        if (std::isnan(v) || std::isinf(v)) return false;
        *out = v;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

RenderServer::RenderServer(Config config)
    : config_(std::move(config)) {}

RenderServer::~RenderServer() {
    stop();
}

RenderServer::RenderServer(RenderServer&& other) noexcept
    : config_(std::move(other.config_)),
      listen_fd_(other.listen_fd_),
      last_error_(std::move(other.last_error_)) {
    other.listen_fd_ = kInvalidSocket;
}

RenderServer& RenderServer::operator=(RenderServer&& other) noexcept {
    if (this != &other) {
        stop();
        config_     = std::move(other.config_);
        listen_fd_  = other.listen_fd_;
        last_error_ = std::move(other.last_error_);
        other.listen_fd_ = kInvalidSocket;
    }
    return *this;
}

bool RenderServer::start() {
    // Already-listening check. Must use the explicit
    // `!= kInvalidSocket` form because on Windows `socket_t` is
    // `SOCKET` (unsigned UINT_PTR) - the legacy `listen_fd_ >= 0`
    // form is always true on Windows (unsigned types are always
    // non-negative) and would short-circuit the very first
    // `start()` call into a no-op success, leaving `listen_fd_`
    // at `kInvalidSocket` and the caller's serve loop with
    // `is_listening() == false`. See
    // `docs/SERVER_LIFETIME_FIX.md` for the symptom this caused
    // ("renderer server stopped (0 requests served)" immediately
    // on Windows).
    if (listen_fd_ != kInvalidSocket) {
        return true;  // already listening
    }
    last_error_.clear();

    if (config_.port <= 0 || config_.port > 65535) {
        last_error_ = "invalid port: " + std::to_string(config_.port);
        return false;
    }

    const socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) {
        last_error_ = "socket() failed: " + lastSocketErrorMessage();
        return false;
    }

    // Allow quick re-bind after a previous server instance shut
    // down (default TIME_WAIT can hold the port for a minute).
    // Winsock's setsockopt takes `const char*` for `optval`; the
    // POSIX prototype is `const void*`. A `char*` cast satisfies
    // both ABIs without UB.
    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse),
                     sizeof reuse) != 0) {
        last_error_ = "setsockopt(SO_REUSEADDR) failed: "
                    + lastSocketErrorMessage();
        closeSocket(fd);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<std::uint16_t>(config_.port));
    if (::inet_pton(AF_INET, config_.bind_address.c_str(),
                    &addr.sin_addr) != 1) {
        last_error_ = "inet_pton failed for bind address: "
                    + config_.bind_address;
        closeSocket(fd);
        return false;
    }

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr),
               sizeof addr) != 0) {
        last_error_ = "bind() failed: " + lastSocketErrorMessage();
        closeSocket(fd);
        return false;
    }
    if (::listen(fd, /*backlog=*/16) != 0) {
        last_error_ = "listen() failed: " + lastSocketErrorMessage();
        closeSocket(fd);
        return false;
    }

    listen_fd_ = fd;
    // Reset the cross-session stop flag. A previous run that
    // exited via the `shutdown` wire command leaves
    // `shutdown_requested_` set; clearing it here lets the
    // same RenderServer instance be reused for a fresh
    // session.
    shutdown_requested_ = false;
    return true;
}

void RenderServer::stop() noexcept {
    if (listen_fd_ != kInvalidSocket) {
        closeSocket(listen_fd_);
        listen_fd_ = kInvalidSocket;
    }
}

std::string RenderServer::handle_command(const std::string& command) {
    const ParsedCommand p = parse_command_line(command);

    if (p.verb == "ping") {
        // Stage 15A.1 baseline; arguments (if any) are ignored.
        return "pong";
    }
    if (p.verb == "shutdown") {
        // Test-only escape hatch (see docs/SHELL_HANG_AUDIT.md).
        // Sets a flag the CLI's serve loop reads between cycles
        // to exit gracefully without needing SIGINT. The
        // response is sent before the loop exits, so the
        // calling client always sees a clean
        // `ok: shutting down` line.
        shutdown_requested_ = true;
        return "ok: shutting down";
    }
    if (p.verb == "set_beta") {
        // Stage 15B.3: scalar |beta| update for the loaded
        // scene, clamped through the existing
        // `rr::relativity::clampBeta` utility. No new relativity
        // math is introduced - the command is a thin wrapper.
        if (!loaded_scene_.has_value()) {
            return "error: no scene loaded; call load_scene first";
        }
        if (p.args.empty()) {
            return "error: set_beta requires a value";
        }
        float requested = 0.0f;
        if (!parse_finite_float(p.args, &requested)) {
            return "error: invalid beta value: " + p.args;
        }

        // Fold to magnitude (the user may have typed a negative
        // number; we want |beta|), then run through the existing
        // clamp against the scene's `max_beta` cap. clampBeta
        // already handles negative inputs and the global ceiling.
        const float magnitude = (requested < 0.0f) ? -requested : requested;
        auto&       scene     = loaded_scene_.value();
        const float clamped   = rr::relativity::clampBeta(
            magnitude, scene.relativity.max_beta);

        // Project the clamped magnitude onto the scene's current
        // velocity direction, preserving the loaded direction
        // (sign + axis). When the velocity is the zero vector
        // there is no direction to preserve; place the new
        // magnitude along camera-forward (-Z), matching the
        // convention `--render-relativistic` uses (see
        // src/main.cpp's `run_render_relativistic`).
        rr::math::Vec3& v = scene.observer.velocity;
        const float current_len_sq =
            v.x * v.x + v.y * v.y + v.z * v.z;
        rr::math::Vec3 new_v;
        if (current_len_sq > 1.0e-24f) {
            const float current_len = std::sqrt(current_len_sq);
            const float k           = clamped / current_len;
            new_v = rr::math::Vec3{v.x * k, v.y * k, v.z * k};
        } else {
            new_v = rr::math::Vec3{0.0f, 0.0f, -clamped};
        }
        v = new_v;

        std::string msg = "ok: beta set magnitude=" + std::to_string(clamped);
        msg += " velocity=" + std::to_string(new_v.x);
        msg += "," + std::to_string(new_v.y);
        msg += "," + std::to_string(new_v.z);
        return msg;
    }
    if (p.verb == "render") {
        // Stage-15-render: dispatch the GPU render pipeline for the
        // currently-loaded scene and save the result to a hard-coded
        // path. Mirrors `run_render_from_scene`'s host-side
        // orchestration in `main.cpp` (load_scene already happened
        // separately via the `load_scene` wire command); the only
        // host work here is per-render: build POD arrays, upload to
        // GpuScene, launch the kernel, save the PPM. All per-pixel
        // / per-ray work runs on the GPU.
        if (!loaded_scene_.has_value()) {
            return "error: no scene loaded";
        }
#ifndef RR_HAS_CUDA
        return "error: render requires CUDA "
               "(rebuild with -DRR_ENABLE_CUDA=ON)";
#else
        const auto& scene  = loaded_scene_.value();
        const int   width  = scene.render_settings.width;
        const int   height = scene.render_settings.height;
        if (width <= 0 || height <= 0) {
            return "error: scene has invalid render dimensions";
        }

        std::vector<rr::geometry::Sphere> sphere_pods;
        sphere_pods.reserve(scene.spheres.size());
        for (const auto& s : scene.spheres) {
            if (s.object.visible) sphere_pods.push_back(s.geometry);
        }

        std::vector<rr::material::MaterialParams> material_pods;
        material_pods.reserve(scene.materials.size());
        for (const auto& m : scene.materials) {
            material_pods.push_back(m.params);
        }

        std::vector<rr::lighting::Light> light_pods;
        light_pods.reserve(scene.lights.size());
        for (const auto& l : scene.lights) {
            if (l.object.visible) light_pods.push_back(l.data);
        }

        rr::gpu::GpuScene gpu_scene;
        if (!gpu_scene.upload_camera(scene.camera)) {
            return "error: render failed at upload_camera";
        }
        if (!gpu_scene.upload_relativity(scene.observer, scene.relativity)) {
            return "error: render failed at upload_relativity";
        }
        if (!sphere_pods.empty()) {
            if (!gpu_scene.upload_spheres(sphere_pods.data(),
                                          sphere_pods.size())) {
                return "error: render failed at upload_spheres";
            }
        }
        if (!material_pods.empty()) {
            if (!gpu_scene.upload_materials(material_pods.data(),
                                            material_pods.size())) {
                return "error: render failed at upload_materials";
            }
        }
        if (!light_pods.empty()) {
            if (!gpu_scene.upload_lights(light_pods.data(),
                                          light_pods.size())) {
                return "error: render failed at upload_lights";
            }
        }

        auto r = rr::cuda::CudaRenderer::render_scene(gpu_scene,
                                                      width, height);
        if (!r.ok) {
            return "error: render failed: " + r.message;
        }

        // Hard-coded output path per the Stage-15-render spec.
        const std::string out_path = "output/server_render.ppm";
        namespace fs = std::filesystem;
        const fs::path out_fs = out_path;
        if (out_fs.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(out_fs.parent_path(), ec);
            // Best-effort directory creation. A genuine permission
            // failure surfaces below at save_ppm time anyway.
        }
        if (!r.image.save_ppm(out_fs)) {
            return "error: render save failed: " + out_path;
        }

        return "ok: rendered " + out_path;
#endif
    }
    if (p.verb == "load_scene") {
        if (p.args.empty()) {
            return "error: load_scene requires a path";
        }
        // Forward to the existing host-side parser. On success
        // we replace the stored scene atomically; on failure the
        // previously-loaded scene (if any) stays put - the
        // protocol's "atomic load" contract.
        rr::io::LoadResult lr = rr::io::load(p.args);
        if (!lr.ok) {
            std::string msg = "error: scene load failed: ";
            msg += lr.error_message.empty() ? "(unknown)" : lr.error_message;
            if (lr.error_line > 0) {
                msg += " (line " + std::to_string(lr.error_line)
                     + ", column " + std::to_string(lr.error_column) + ")";
            }
            return msg;
        }
        // Build the summary BEFORE moving the scene into the
        // member - reading `lr.scene`'s vectors after a move-from
        // would observe empty containers (a "valid but
        // unspecified" state for `std::vector`).
        const auto& s = lr.scene;
        std::string summary = "ok: scene loaded";
        summary += " width="     + std::to_string(s.render_settings.width);
        summary += " height="    + std::to_string(s.render_settings.height);
        summary += " materials=" + std::to_string(s.materials.size());
        summary += " spheres="   + std::to_string(s.spheres.size());
        summary += " meshes="    + std::to_string(s.meshes.size());
        summary += " lights="    + std::to_string(s.lights.size());

        loaded_scene_ = std::move(lr.scene);
        return summary;
    }

    return "error: unknown command";
}

RenderServer::ServeResult RenderServer::serve_one() {
    ServeResult r;

    if (listen_fd_ == kInvalidSocket) {
        r.error_message = "not started";
        return r;
    }

    sockaddr_in client_addr{};
    // Winsock declares `socklen_t` as `int`; POSIX as `unsigned
    // int`. Cast the `sizeof` (which is `size_t`) explicitly so
    // both ABIs accept it without narrowing-conversion warnings.
    socklen_t   client_len = static_cast<socklen_t>(sizeof client_addr);
    socket_t    client_fd  = kInvalidSocket;
    while (true) {
        client_fd = ::accept(listen_fd_,
                             reinterpret_cast<sockaddr*>(&client_addr),
                             &client_len);
        if (client_fd != kInvalidSocket) break;
        if (socketWasInterrupted()) continue;
        r.error_message = "accept() failed: " + lastSocketErrorMessage();
        return r;
    }

    // Capture the client identity for the result struct.
    {
        char buf[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &client_addr.sin_addr,
                        buf, sizeof buf) != nullptr) {
            r.client_address = buf;
        }
        r.client_port = ntohs(client_addr.sin_port);
    }

    auto rl = read_line(client_fd, kMaxCommandBytes);
    if (!rl.ok) {
        // Best-effort error response back to the client.
        const std::string err_response = rl.error + "\n";
        (void)write_all(client_fd, err_response);
        closeSocket(client_fd);
        r.error_message = std::move(rl.error);
        return r;
    }

    r.command  = rl.line;
    r.response = handle_command(r.command);

    const std::string payload = r.response + "\n";
    if (!write_all(client_fd, payload)) {
        const std::string werr = lastSocketErrorMessage();
        closeSocket(client_fd);
        r.error_message = "write failed: " + werr;
        return r;
    }

    closeSocket(client_fd);
    r.ok = true;
    return r;
}

}  // namespace rr::server
