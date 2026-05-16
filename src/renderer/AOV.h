#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rr::renderer {

// Stable integer handle for an AOV entry in a future scene-side AOV
// table. -1 means "not yet registered". Mirrors the convention used
// by `rr::texture::TextureId` and `rr::material::Material`'s
// material index: downstream code (a renderer pass list, an output-
// file selector) refers to AOVs by id, not by pointer, so lifetime
// stays decoupled from the data model.
using AOVId = std::int32_t;

inline constexpr AOVId kInvalidAOVId = -1;

// Discriminator for the unified `AOV` type.
//
// Stage 14A.1 (master order #19) ships the six render passes the
// prompt requires. The eventual renderer integration (a separate
// sub-stage) walks an AOV array, allocates a per-pass framebuffer
// the right size for that pass's component count, and writes into
// it from the kernel.
//
// Component layouts (consumed by `aov_component_count` below):
//
// - Beauty            : RGB (3 floats). Final shaded radiance.
// - Normal            : XYZ (3 floats). Surface shading normal at the
//                       hit, in world space, components in [-1, 1].
// - Depth             : single float. World-space distance from
//                       camera to the hit, or +inf on miss; the
//                       renderer can store ray `t`, view-space Z,
//                       or true distance - the choice is the
//                       renderer-integration sub-stage's, not the
//                       data model's.
// - Albedo            : RGB (3 floats). Base colour at the hit
//                       *before* lighting, after texture lookup.
//                       Useful as a denoising guide buffer.
// - DopplerFactor     : single float. The relativistic frequency
//                       ratio D = omega_observer / omega_emitter
//                       (per `rr::relativity::dopplerFactor`).
// - SearchlightFactor : single float. The relativistic intensity
//                       multiplier D^4 (per `rr::relativity::
//                       searchlightFactor`). Stored as a separate
//                       AOV so authors can post-process the beam
//                       brightness without having to recompute
//                       from `DopplerFactor`.
//
// Enumerator naming follows the project's PascalCase convention
// for enum values (matches `LightType`, `TextureKind`,
// `ImageTextureFormat`); the prompt's mixed-case type list is
// conceptual.
enum class AOVType : std::uint32_t {
    Beauty            = 0,
    Normal            = 1,
    Depth             = 2,
    Albedo            = 3,
    DopplerFactor     = 4,
    SearchlightFactor = 5,
    // MANI-I.8 — manifold debug coordinate-visualisation AOV.
    // Writes a 3-component (Vec3) per-pixel value carrying the
    // chart-space hit position for the active manifold mode.
    // On the Euclidean / disabled default this equals the
    // world-space hit position (the documented identity /
    // neutral visualisation); future curved-chart slices make
    // the AOV's pixel values diverge from world-space hit
    // positions so an operator can *see* the chart's
    // coordinate deformation. Miss pixels write `(0, 0, 0)`,
    // matching the Normal AOV's miss convention. Opt-in:
    // allocated only when the operator passes
    // `--render-aovs --manifold-debug` (see
    // `docs/MANIFOLD_DEBUG_AOV_TASK.md`).
    ManifoldCoordinates = 6,
    // OBSERVER.13 — observer-frame debug-visualisation AOV.
    // Writes a 3-component (Vec3) per-pixel value carrying
    // the active observer's `ObserverFrame::beta` (the
    // OBSERVER.8 + OBSERVER.10 carry-only field, populated
    // by the OBSERVER.6 camera-to-observer adapter from the
    // CLI / scene observer config). On the default
    // `PerceptionMode::Identity` mode this equals `(0, 0, 0)`
    // at every pixel (the no-op anchor). On
    // `ConstantVelocityMinkowski` mode with a non-zero
    // observer beta, every hit pixel writes the same
    // `(beta.x, beta.y, beta.z)` value — a flat colour
    // confirming the kernel saw the per-launch observer
    // payload intact. Miss pixels write `(0, 0, 0)`. Opt-in:
    // allocated only when the operator passes
    // `--render-aovs --observer-debug` (CUDA) /
    // `--render-optix-aovs --observer-debug` (OptiX) (see
    // `docs/OBSERVER_DEBUG_AOV_TASK.md`). Read-only
    // diagnostic — the kernel does NOT apply any
    // perception transform (no aberration / Doppler /
    // searchlight re-keying on the observer-frame beta);
    // that is reserved for a separate future slice.
    ObserverBeta = 7,
};

// Number of float channels an `AOVType` writes per pixel. Used by
// the eventual renderer-integration sub-stage to size per-pass
// framebuffers; today it is purely informational + lets host code
// prevalidate AOV requests against `Image::PixelFormat`'s
// 1-channel / 3-channel / 4-channel options.
[[nodiscard]] int aov_component_count(AOVType type) noexcept;

// Stable, lowercase identifier suitable for filenames + log output
// + scene-format authoring. Returns a `string_view` into a static
// constant; never null, never owning.
//
//   Beauty            -> "beauty"
//   Normal            -> "normal"
//   Depth             -> "depth"
//   Albedo            -> "albedo"
//   DopplerFactor     -> "doppler_factor"
//   SearchlightFactor -> "searchlight_factor"
//
// An unknown enumerator (out-of-range cast) returns "unknown".
[[nodiscard]] std::string_view aov_type_name(AOVType type) noexcept;

// Host-side AOV descriptor. Plain data, copy-friendly.
//
// Stage 14A.1 scope: data model only. There is no per-pass
// framebuffer here, no kernel hook, no upload path. Subsequent
// 14A+ sub-stages add the renderer integration that allocates +
// fills + saves each pass.
//
// Field semantics:
// - `id` is the stable handle the eventual scene-side AOV table
//   indexes by. -1 means uninitialised.
// - `type` selects which kernel write path (and how many
//   components per pixel) the eventual renderer integration
//   uses.
// - `name` is for authoring / debugging / output filename
//   selection. Defaults to the lowercase form of `type` (set by
//   the factory functions). The GPU side never sees it.
class AOV {
public:
    AOV() = default;

    [[nodiscard]] static AOV make_beauty(std::string name = {});
    [[nodiscard]] static AOV make_normal(std::string name = {});
    [[nodiscard]] static AOV make_depth(std::string name = {});
    [[nodiscard]] static AOV make_albedo(std::string name = {});
    [[nodiscard]] static AOV make_doppler_factor(std::string name = {});
    [[nodiscard]] static AOV make_searchlight_factor(std::string name = {});
    // MANI-I.8 — manifold debug coordinate-visualisation AOV
    // factory. Returns an `AOV` with
    // `type() == AOVType::ManifoldCoordinates` and
    // `name() == "manifold_coordinates"` (or the caller-
    // supplied name).
    [[nodiscard]] static AOV make_manifold_coordinates(std::string name = {});
    // OBSERVER.13 — observer debug AOV factory. Returns an
    // `AOV` with `type() == AOVType::ObserverBeta` and
    // `name() == "observer_beta"` (or the caller-supplied
    // name). Mirrors the `make_manifold_coordinates(...)`
    // factory shape verbatim.
    [[nodiscard]] static AOV make_observer_beta(std::string name = {});

    [[nodiscard]] AOVId              id()              const noexcept { return id_; }
    [[nodiscard]] AOVType            type()            const noexcept { return type_; }
    [[nodiscard]] const std::string& name()            const noexcept { return name_; }

    // Convenience: the per-pixel float channel count for this AOV's
    // type. Same value as `aov_component_count(type())`.
    [[nodiscard]] int                component_count() const noexcept;

    void set_id(AOVId id) noexcept;
    void set_name(std::string name);

private:
    AOVId       id_   = kInvalidAOVId;
    AOVType     type_ = AOVType::Beauty;
    std::string name_;
};

}  // namespace rr::renderer
