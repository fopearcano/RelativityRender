#pragma once

#include "material/graph/Node.h"
#include "material/graph/Socket.h"

#include <cstdint>
#include <string>
#include <vector>

// Material graph data core - graph layer.
//
// Plain-data container for a v1 material graph: a list of nodes
// and an explicit list of connections between them, plus a
// `version` field per spec section 10.3 (graph-block versioning).
//
// Connections are stored at the graph level rather than on the
// individual nodes so the data shape mirrors what a future scene-
// format slice will serialise: a flat `nodes[]` + `connections[]`
// pair, both addressable by integer index.
//
// `validate_graph` runs the structural validation the spec's
// section 7 already pinned (unique ids, references resolve,
// type-compatible connections, DAG, at least one terminal). It
// does NOT compile, lower, or evaluate - the runtime side
// (`MaterialGraph.cpp` from the previous slice; the eventual
// dedicated `Compile.cpp`) consumes a validated graph and goes
// from there.

namespace rr::material::graph {

// One edge of the graph: a typed connection from `from_node`'s
// `from_socket` (an Output socket) to `to_node`'s `to_socket`
// (an Input socket).
//
// Trivially serialisable: four plain fields, two integer node
// ids, two socket names. Direction is implicit (Output -> Input);
// the validator confirms `from` resolves to an output and `to`
// resolves to an input.
struct Connection {
    NodeId      from_node = kInvalidNodeId;
    std::string from_socket;
    NodeId      to_node   = kInvalidNodeId;
    std::string to_socket;
};


// Result of a structural validation. Pure data: the validator
// runs no IO and mutates nothing. Declared here (before `Graph`)
// because `Graph::validate()` returns one by value.
struct ValidationResult {
    bool        ok = false;
    std::string message;        // populated when `ok == false`.
};


// Plain-data graph. The data layer's complete public surface.
// Owned by value; no shared / reference semantics. Trivially
// copyable apart from its embedded `std::string`s and
// `std::vector`s.
//
// `version` carries the graph-block version per spec section
// 10.3. v1 is the only supported value today; loaders that see
// a future version reject the load (per the same spec section's
// rule) before the data ever reaches this struct.
//
// The struct's `nodes` and `connections` vectors stay public so
// callers (the .rrscene loader, the C4D bridge, the eventual
// node editor) can populate them by direct vector ops when
// that's more natural. The builder helpers below
// (`add_node` / `connect` / `validate`) are convenience
// alternatives - they manipulate the same fields with auto-id
// allocation, immediate sanity-checks, and a `validate()`
// shorthand for `validate_graph(*this)`.
struct Graph {
    int                     version = 1;
    std::vector<Node>       nodes;
    std::vector<Connection> connections;

    // Append a new node of `type` with the catalogue's
    // canonical socket layout (see `make_node` in Node.h) and
    // return its id. The id is auto-allocated as one greater
    // than the largest existing id (so successive `add_node`
    // calls never collide), or `0` when the graph is empty.
    NodeId add_node(NodeType type);

    // Append a connection from `from_node`'s output socket to
    // `to_node`'s input socket. Performs immediate sanity
    // checks at insertion time:
    //
    //   - both nodes exist in this graph;
    //   - `from_socket` resolves to an Output socket on
    //     `from_node`;
    //   - `to_socket` resolves to an Input socket on
    //     `to_node`;
    //   - the source / sink socket types are
    //     `can_connect`-compatible (per spec 7.3 implicit
    //     conversions);
    //   - `to_socket` is not already wired by an earlier
    //     connection.
    //
    // Returns true on success and appends to `connections`;
    // returns false (without appending) when any check fails.
    // Cycle detection and the full structural validation are
    // NOT run here (they belong to `validate()`); a connection
    // that closes a cycle still appends successfully and is
    // caught at validate time.
    bool connect(NodeId           from_node,
                 std::string_view from_socket,
                 NodeId           to_node,
                 std::string_view to_socket);

    // Method form of the free `validate_graph(*this)` below.
    // The two are functionally identical; the method form
    // reads more naturally on built-up graphs.
    [[nodiscard]] ValidationResult validate() const;
};


// Look up a node by id. Returns nullptr when no node with that
// id exists. Both overloads are provided so const-correctness
// flows naturally from the caller.
[[nodiscard]] const Node* find_node(const Graph& graph, NodeId id);
[[nodiscard]] Node*       find_node(Graph& graph, NodeId id);


// Structural validation per spec section 7. Checks:
//
//   1. Every `node.id` is unique within the graph.
//   2. Every connection's `from_node` / `to_node` resolves to
//      an existing node.
//   3. Every connection's `from_socket` resolves to an Output
//      socket on `from_node`, and `to_socket` resolves to an
//      Input socket on `to_node`.
//   4. Each Input socket has at most ONE incoming connection
//      (per spec 7.3 rule "the sink is not already wired").
//   5. The connection's source / sink socket types are
//      `can_connect`-compatible (per spec section 7.3's
//      implicit-conversion table).
//   6. The graph contains no cycles (per spec section 7.4: a
//      DAG is required).
//   7. The graph contains at least one terminal node.
//   8. Every input socket marked `required` is wired by some
//      connection. The v1 catalogue marks NO sockets required
//      (spec 7.1 keeps inputs optional and falls back to
//      catalogue defaults), so this check is a no-op for v1
//      graphs; it is the place a future "required-input"
//      node type would surface unfilled inputs.
//
// Returns `ok == true` and an empty `message` on success;
// `ok == false` with a descriptive message on the FIRST
// detected violation. The order of checks is fixed so error
// messages are stable across runs.
[[nodiscard]] ValidationResult validate_graph(const Graph& graph);


// Convenience: enumerate a node's incoming connections (the
// connections whose `to_node` matches `id`). Useful for
// callers that walk the graph by node rather than by edge.
// Returned pointers reference entries in `graph.connections`
// and are invalidated by any operation that resizes that
// vector.
[[nodiscard]] std::vector<const Connection*>
incoming_connections(const Graph& graph, NodeId id);

}
