#pragma once

#include "material/MaterialTypes.h"
#include "math/Vec3.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Forward-declare the data-core graph type so this header
// stays lightweight. The compile function takes a
// `graph::Graph` by const-ref; the caller already includes
// `material/graph/Graph.h` to build one.
namespace rr::material::graph { struct Graph; }

// GPU-friendly material representation.
//
// This is the IR the spec's section 9.2 calls for: an
// operation list + a terminal table, both as flat fixed-shape
// records. The host's `compile_graph_to_gpu_material(...)`
// turns a validated `graph::Graph` into one of these; the
// device-side launch path (a future slice) reads the IR
// through a `rr::cuda::CudaMaterialGraphView` of raw device
// pointers exposed in `cuda/CudaMaterialGraph.cuh`.
//
// Three rules the design honours:
//
//   1. No pointers between GPU records. Every cross-record
//      reference is an integer slot index. The kernel
//      reaches the data through device pointers + counts;
//      records reference each other only by index.
//   2. Fixed per-record shape. `GpuOp` and `GpuTerminal`
//      are PODs of constant size. Their bytes upload as-is
//      to a `GpuBuffer<GpuOp>` / `GpuBuffer<GpuTerminal>`
//      with no further marshalling.
//   3. Compact storage for parameters. Immediates live
//      INSIDE each record - no separate constant pool, no
//      string tables, no symbol table. The records ARE the
//      parameters.
//
// This slice ships the host POD + the lowering. v1 does NOT
// yet execute the IR on the GPU; that is a future slice.
// `debug_print_gpu_material` exists so the test harness and
// any future authoring tool can dump the lowered IR for
// inspection without booting the renderer.

namespace rr::material {

// Per-record opcode discriminator. Mirrors `NodeType` from
// the data core (`material/graph/Node.h`) but kept as its
// own enum so the GPU records stay independent of the
// authoring data shape - if the catalogue grows non-IR
// node types, the discriminator on the GPU side does not
// inherit them automatically.
enum class GpuOpcode : std::uint8_t {
    // Operation opcodes (produce a slot value).
    ConstantColor = 0,
    TextureSample = 1,
    Add           = 2,
    Multiply      = 3,

    // Terminal opcodes (consumed by the renderer to compose
    // the per-hit `MaterialParams`).
    Diffuse       = 4,
    Emission      = 5,
};

[[nodiscard]] const char* gpu_opcode_name(GpuOpcode op);


// One operation record. Fixed size; no pointers; the input
// slots are integer indices into the operation array's
// own output positions (slot index = position of the
// producing op in `GpuMaterial::ops`).
//
// Per-opcode field usage:
//
//   ConstantColor : `imm_color` (no inputs).
//   TextureSample : `imm_int` (texture id), `imm_color`
//                   (the placeholder fallback colour the
//                   future kernel returns when the bound
//                   texture is missing). Inputs unused in
//                   v1 (no vec2 producer).
//   Add           : `in_a`, `in_b` slot indices; immediates
//                   ignored. Unwired falls back to additive
//                   identity (zero) at execution time.
//   Multiply      : `in_a`, `in_b` slot indices; immediates
//                   ignored. Unwired falls back to
//                   multiplicative identity (one).
struct GpuOp {
    GpuOpcode      opcode;             //  1 byte
    std::uint8_t   _pad0[1] = {0};     //  pad to align in16
    std::int16_t   in_a    = -1;       //  2 bytes; -1 = unwired
    std::int16_t   in_b    = -1;       //  2 bytes; -1 = unwired
    std::uint8_t   _pad1[2] = {0, 0};  //  pad to align vec3
    rr::math::Vec3 imm_color{0, 0, 0}; // 12 bytes
    std::int32_t   imm_int = -1;       //  4 bytes
};


// Terminal-table entry. Each entry contributes one piece of
// the per-hit `MaterialParams` snapshot the renderer's
// shading code consumes today:
//
//   Diffuse  -> writes `MaterialParams::baseColor` from the
//               resolved albedo (slot or immediate).
//   Emission -> writes `emissionColor` + `emissionStrength`
//               from the resolved colour and strength.
//
// `kind` is one of `GpuOpcode::Diffuse` / `GpuOpcode::Emission`.
// Other opcodes are illegal as terminals and the lowering
// rejects them upstream.
//
// `in_color` / `in_strength` are slot indices (`-1` = use
// the immediate fallback). Immediates are always populated;
// they cover the unwired-input case so the kernel does not
// need a "wired?" branch.
struct GpuTerminal {
    GpuOpcode      kind;                //  1 byte
    std::uint8_t   _pad0[1] = {0};
    std::int16_t   in_color    = -1;    // -1 = unwired
    std::int16_t   in_strength = -1;    // Emission only; Diffuse leaves at -1
    std::uint8_t   _pad1[2] = {0, 0};
    rr::math::Vec3 imm_color{0, 0, 0};  // default for `color`
    float          imm_strength = 0.0f; // default for `strength`
};


// Compiled per-material IR. The two arrays are independent;
// they upload to separate device buffers in a future slice
// and are read by the kernel through a
// `CudaMaterialGraphView` (`cuda/CudaMaterialGraph.cuh`).
//
// `slot_count` mirrors `ops.size()`. It is recorded
// explicitly so the future kernel can stack-allocate a
// fixed-size scratch slot pool without re-deriving the
// count from the array.
struct GpuMaterial {
    std::vector<GpuOp>       ops;
    std::vector<GpuTerminal> terminals;
    int                      slot_count = 0;
};


struct GpuMaterialResult {
    bool        ok = false;
    std::string message;        // populated on `ok == false`
    GpuMaterial material{};     // populated on `ok == true`
};

// Compile a validated `graph::Graph` to a `GpuMaterial`.
//
// The lowering:
//   1. Runs `validate_graph` and rejects any graph that
//      does not pass.
//   2. Topologically orders the nodes reachable from at
//      least one terminal (per spec 7.4 / 8.3 dead-code
//      drop).
//   3. Assigns each non-terminal reachable node a slot
//      index = its position in the topo order.
//   4. Emits one `GpuOp` per non-terminal in topo order.
//      Wired inputs become slot indices; unwired inputs
//      stay `-1` (the kernel-side execution will treat
//      `-1` as "use the per-opcode identity / immediate").
//   5. Emits one `GpuTerminal` per terminal node, with
//      slot indices for wired inputs and immediates for
//      unwired inputs (so the kernel never branches on
//      "wired?").
//
// Returns `ok == false` with a descriptive `message`
// when validation fails, when the topo sort detects a
// cycle (defence-in-depth - validation already rejects
// cycles), or when more nodes are reachable than the
// `int16` slot index can address (limit ~32k - way above
// any realistic v1 graph).
[[nodiscard]] GpuMaterialResult compile_graph_to_gpu_material(
    const graph::Graph& graph);

// Synthesise a v1 graph from an existing flat `MaterialParams`
// + compile it to a `GpuMaterial` IR. The synthesised graph is
// the smallest one that produces the same per-hit baseColor /
// emissionColor / emissionStrength values the kernel reads
// today:
//
//   ConstantColor(baseColor) -> DiffuseBSDF.albedo
//
// plus, when `emissionStrength > 0`:
//
//   ConstantColor(emissionColor) -> Emission.color
//                                   (strength = emissionStrength)
//
// Used by `GpuScene::upload_material_graphs` so existing
// scenes (whose materials are flat structs today) get a
// graph-shaped representation on the GPU without authoring
// changes. Returns a default-baked `GpuMaterial` (one
// ConstantColor + one Diffuse terminal at mid-grey) when
// `params == nullptr`.
[[nodiscard]] GpuMaterial synthesise_gpu_material_from_params(
    const MaterialParams* params);


// Print a human-readable dump of `mat` to `out` (defaults
// to stdout). Format:
//
//   GpuMaterial: ops=N terminals=T slot_count=N
//     ops:
//       [0] ConstantColor      imm_color=(0.700, 0.400, 0.200)
//       [1] Multiply           in_a=0 in_b=2
//       ...
//     terminals:
//       [0] Diffuse            in_color=1 imm_color=(0.800, 0.800, 0.800)
//       [1] Emission           in_color=2 in_strength=-1 imm_strength=2.000
//
// Single-pass; no allocations beyond `fprintf`. The format
// is informational; a future scene-format slice's text
// representation will be specified separately.
void debug_print_gpu_material(const GpuMaterial& mat,
                              std::FILE* out = stdout);

}
