#pragma once

// Cross-platform socket header + helpers for the renderer
// server (Stage 15* sub-stages). Stages 15A.1 / 15A.2 / 15B.1 /
// 15B.3 targeted POSIX sockets directly; this slice introduces
// a thin shim so the same code compiles on both Linux/macOS
// (POSIX) and Windows (Winsock2). Pure portability fix - no
// new server features, no new commands, no behaviour change.
//
// Linux/macOS:
//   - Headers: <arpa/inet.h>, <netinet/in.h>, <sys/socket.h>,
//              <unistd.h>
//   - socket_t = int                  ; kInvalidSocket = -1
//   - kSocketShutdownBoth = SHUT_RDWR
//   - closeSocket          -> ::close
//   - initSocketSystem     -> no-op (returns true)
//   - shutdownSocketSystem -> no-op
//
// Windows:
//   - Headers: <winsock2.h>, <ws2tcpip.h>
//   - socket_t = SOCKET               ; kInvalidSocket = INVALID_SOCKET
//   - kSocketShutdownBoth = SD_BOTH
//   - closeSocket          -> ::closesocket
//   - initSocketSystem     -> WSAStartup(MAKEWORD(2, 2), &wsa)
//   - shutdownSocketSystem -> WSACleanup
//   - CMake links Ws2_32 into rr_server.
//
// All callers in the project go through this header instead of
// raw platform headers. The names are camelCase to match the
// Windows-binding style; the rest of the project's free
// functions stay snake_case.

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstring>
#endif

#include <string>

namespace rr::server {

#if defined(_WIN32)
using socket_t = SOCKET;
inline constexpr socket_t kInvalidSocket      = INVALID_SOCKET;
inline constexpr int      kSocketShutdownBoth = SD_BOTH;
#else
using socket_t = int;
inline constexpr socket_t kInvalidSocket      = -1;
inline constexpr int      kSocketShutdownBoth = SHUT_RDWR;
#endif

// Initialise the platform's socket subsystem. On Windows, calls
// `WSAStartup(MAKEWORD(2, 2), ...)` and returns the success
// flag; on POSIX, no-op returning `true`. Pair with
// `shutdownSocketSystem()` at the same scope.
[[nodiscard]] inline bool initSocketSystem() noexcept {
#if defined(_WIN32)
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

// Tear down the platform's socket subsystem. On Windows, calls
// `WSACleanup`. On POSIX, no-op.
inline void shutdownSocketSystem() noexcept {
#if defined(_WIN32)
    ::WSACleanup();
#endif
}

// Close a socket. Returns 0 on success.
inline int closeSocket(socket_t s) noexcept {
#if defined(_WIN32)
    return ::closesocket(s);
#else
    return ::close(s);
#endif
}

// True iff the most recent socket op was interrupted by a
// signal (POSIX) / aborted by WSACancelBlockingCall (Windows).
// Call immediately after a recv/send/accept that returned a
// negative value.
[[nodiscard]] inline bool socketWasInterrupted() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

// Last platform-level socket error rendered as a printable
// string. POSIX uses `strerror(errno)`; Windows uses the WSA
// error code (numeric form is the cheapest stable identifier).
inline std::string lastSocketErrorMessage() {
#if defined(_WIN32)
    return "WSA error " + std::to_string(::WSAGetLastError());
#else
    const char* s = std::strerror(errno);
    return s ? std::string(s) : ("errno " + std::to_string(errno));
#endif
}

}  // namespace rr::server
