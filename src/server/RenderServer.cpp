#include "server/RenderServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

namespace rr::server {

namespace {

// errno -> std::string. Uses strerror_r when available; we keep
// the call thread-unsafe (single-threaded server today), but the
// formatting is robust to a missing entry.
std::string errno_message(int err) {
    const char* s = std::strerror(err);
    return s ? std::string(s) : std::string("unknown error ")
                              + std::to_string(err);
}

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

ReadLineResult read_line(int fd, std::size_t max_bytes) {
    std::string acc;
    acc.reserve(64);
    std::array<char, 64> buf{};

    while (acc.size() < max_bytes) {
        const std::size_t want =
            std::min(buf.size(), max_bytes - acc.size());
        const ssize_t n = ::recv(fd, buf.data(), want, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return {false, {}, "io error: " + errno_message(errno)};
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
bool write_all(int fd, const std::string& payload) {
    const char* p = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t n = ::send(fd, p, remaining, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
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

// Map a command string to its response (without trailing newline).
std::string handle_command(const std::string& command) {
    if (command == "ping") {
        return "pong";
    }
    return "error: unknown command";
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
    other.listen_fd_ = -1;
}

RenderServer& RenderServer::operator=(RenderServer&& other) noexcept {
    if (this != &other) {
        stop();
        config_     = std::move(other.config_);
        listen_fd_  = other.listen_fd_;
        last_error_ = std::move(other.last_error_);
        other.listen_fd_ = -1;
    }
    return *this;
}

bool RenderServer::start() {
    if (listen_fd_ >= 0) {
        return true;  // already listening
    }
    last_error_.clear();

    if (config_.port <= 0 || config_.port > 65535) {
        last_error_ = "invalid port: " + std::to_string(config_.port);
        return false;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        last_error_ = "socket() failed: " + errno_message(errno);
        return false;
    }

    // Allow quick re-bind after a previous server instance shut
    // down (default TIME_WAIT can hold the port for a minute).
    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof reuse) != 0) {
        last_error_ = "setsockopt(SO_REUSEADDR) failed: "
                    + errno_message(errno);
        ::close(fd);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(config_.port));
    if (::inet_pton(AF_INET, config_.bind_address.c_str(),
                    &addr.sin_addr) != 1) {
        last_error_ = "inet_pton failed for bind address: "
                    + config_.bind_address;
        ::close(fd);
        return false;
    }

    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr),
               sizeof addr) != 0) {
        last_error_ = "bind() failed: " + errno_message(errno);
        ::close(fd);
        return false;
    }
    if (::listen(fd, /*backlog=*/16) != 0) {
        last_error_ = "listen() failed: " + errno_message(errno);
        ::close(fd);
        return false;
    }

    listen_fd_ = fd;
    return true;
}

void RenderServer::stop() noexcept {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

RenderServer::ServeResult RenderServer::serve_one() {
    ServeResult r;

    if (listen_fd_ < 0) {
        r.error_message = "not started";
        return r;
    }

    sockaddr_in client_addr{};
    socklen_t   client_len = sizeof client_addr;
    int client_fd = -1;
    while (true) {
        client_fd = ::accept(listen_fd_,
                             reinterpret_cast<sockaddr*>(&client_addr),
                             &client_len);
        if (client_fd >= 0) break;
        if (errno == EINTR) continue;
        r.error_message = "accept() failed: " + errno_message(errno);
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
        ::close(client_fd);
        r.error_message = std::move(rl.error);
        return r;
    }

    r.command  = rl.line;
    r.response = handle_command(r.command);

    const std::string payload = r.response + "\n";
    if (!write_all(client_fd, payload)) {
        ::close(client_fd);
        r.error_message = "write failed: " + errno_message(errno);
        return r;
    }

    ::close(client_fd);
    r.ok = true;
    return r;
}

}  // namespace rr::server
