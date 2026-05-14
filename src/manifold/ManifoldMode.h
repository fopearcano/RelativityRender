#pragma once

// Identity tag for the active coordinate chart. The Manifold Core
// expresses spacetime via a *chart* on the rendered manifold (see
// `docs/MANIFOLD_RENDERING_ARCHITECTURE.md` §3.1 and §4.2), and the
// renderer picks which chart is active per render. This enum names
// the chart slots reserved by the architecture doc.
//
// Only `Identity` has a concrete implementation today; it wraps
// today's renderer behaviour as the Minkowski + constant-velocity-
// frame specialisation of the Manifold Core's contracts (§7.1 of the
// architecture doc). The other entries are reserved-but-inert
// placeholders for future chart milestones; selecting one of them at
// this stage carries no behavioural meaning beyond carrying the tag.
//
// Architecture-doc non-goal §8 ("an empty scaffold for the Manifold
// Core") is satisfied by keeping every non-Identity entry *named*
// without claiming any of its physics has been implemented.

namespace rr::manifold {

enum class ManifoldMode {
    Identity        = 0,  // Minkowski + constant-velocity observer frame.
    Schwarzschild,        // Reserved; not implemented.
    KruskalSzekeres,      // Reserved; not implemented.
    Penrose,              // Reserved; not implemented.
    Kerr,                 // Reserved; not implemented.
};

}
