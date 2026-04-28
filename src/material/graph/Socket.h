#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Material graph data core - socket layer.
//
// `docs/MATERIAL_GRAPH_SPEC.md` section 7 defines a socket as a
// "named, typed connection point on a node". This header is the
// data-only embodiment of that contract: enums + a small POD,
// no evaluation, no GPU.
//
// Per the spec's section 7.2, the v1 type list is closed:
// `float`, `vec2`, `vec3`, `color`, `normal`. The serialised
// form (a future scene-format slice) will mirror these names
// exactly; `socket_type_name` / `parse_socket_type` pin the
// canonical spelling.
//
// Per the spec's section 7.3, not every type pair is connectable.
// `can_connect(source, sink)` answers that for the connection
// validator that the Graph layer runs (see Graph.h). The actual
// implicit conversion (broadcast, reinterpret) happens at
// evaluation time and is NOT this slice's concern.

namespace rr::material::graph {

enum class SocketType : std::uint8_t {
    Float  = 0,
    Vec2   = 1,
    Vec3   = 2,
    Color  = 3,
    Normal = 4,
};

inline constexpr int kSocketTypeCount = 5;

[[nodiscard]] const char* socket_type_name(SocketType type);

// Parse a socket-type name into the enum. Case-sensitive: lower-
// case names are accepted (the spec writes them lowercase), and
// the corresponding PascalCase forms used in the rest of the
// project are also accepted so JSON written either way parses.
[[nodiscard]] bool parse_socket_type(std::string_view name, SocketType& out);

// Implicit-conversion table from spec section 7.3. Returns true
// when a `source`-typed output may drive a `sink`-typed input.
// The runtime applies the actual conversion (broadcast for
// `float -> any`, reinterpret for `vec3 <-> color`, etc.); the
// data layer only knows whether the connection is legal.
[[nodiscard]] bool can_connect(SocketType source, SocketType sink);


enum class SocketDirection : std::uint8_t {
    Input  = 0,
    Output = 1,
};

[[nodiscard]] const char* socket_direction_name(SocketDirection direction);


// Plain-data socket. Owned by its containing `Node` (see Node.h).
// Kept tightly POD so a future scene-format slice can serialise a
// node's socket list verbatim without round-tripping through any
// runtime helper.
//
// The socket's connection state lives at the `Graph` layer: a
// `Connection` references a (node_id, socket_name) pair. The
// socket itself does not know whether it is wired - that question
// is asked of the graph.
//
// `required` is honoured only for input sockets (output sockets
// keep the default `false`, since "wiring" is a sink-side
// concept). When `required == true` the validator rejects the
// graph if no incoming connection terminates at this socket.
// When `required == false` (the spec's default - per section
// 7.1, inputs may be unwired and fall back to the catalogue's
// per-input defaults), the validator accepts an unwired socket
// silently. The v1 catalogue marks NO sockets required; the
// flag exists so a future node type whose input has no sensible
// default can opt in without re-shaping the validator.
struct Socket {
    std::string     name;
    SocketType      type      = SocketType::Color;
    SocketDirection direction = SocketDirection::Input;
    bool            required  = false;
};

}
