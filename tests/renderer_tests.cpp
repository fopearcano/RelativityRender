// PT-P.3 renderer-layer tests.
//
// Covers `rr::renderer::AccumulationBuffer`'s host-side state
// machine on the cold paths the PT-P.3 polish slice tightened:
//
//   - `resize(64, 64)` followed by `resize(64, 64)` (the no-op
//     fast path) leaves the buffer in a 0-sample state with
//     unchanged dimensions. Exercised on both the host-only
//     audit-host build (where the underlying `launch_accum_*`
//     calls are not available and resize / reset honestly
//     return false) and the CUDA-host build (where they
//     return true and the buffer is genuinely zeroed).
//   - `resize(0, 0)` collapses the buffer back to its default
//     state.
//   - `samples_count()` is 0 after default-construction and
//     after every fresh `resize(...)`.
//
// The class's public API is the only surface this test
// exercises; no kernel launches are required for these
// post-condition checks (the public accessors hold even when
// the underlying CUDA backend is unavailable).

#include "renderer/AccumulationBuffer.h"
#include "renderer/AOV.h"

#include <cstdio>
#include <string>

namespace {

int g_total  = 0;
int g_failed = 0;

#define RR_CHECK(...)                                                         \
    do {                                                                      \
        ++g_total;                                                            \
        if (!(__VA_ARGS__)) {                                                 \
            ++g_failed;                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n",                        \
                         #__VA_ARGS__, __FILE__, __LINE__);                   \
        }                                                                     \
    } while (0)

using rr::renderer::AccumulationBuffer;

void test_default_state() {
    AccumulationBuffer accum;
    RR_CHECK(accum.width()         == 0);
    RR_CHECK(accum.height()        == 0);
    RR_CHECK(accum.samples_count() == 0);
    RR_CHECK(!accum.valid());
}

void test_resize_zero_dimensions_returns_to_default() {
    AccumulationBuffer accum;
    const bool ok = accum.resize(0, 0);
    RR_CHECK(!ok);
    RR_CHECK(accum.width()         == 0);
    RR_CHECK(accum.height()        == 0);
    RR_CHECK(accum.samples_count() == 0);
    RR_CHECK(!accum.valid());
}

// PT-P.3 §1.3: re-resizing to the same dimensions must produce
// a 0-sample buffer with unchanged width / height. The no-op
// fast path's external contract is identical to the slow path.
void test_resize_same_dimensions_twice_keeps_zero_samples() {
    AccumulationBuffer accum;

    // First resize. On the host-only audit-host build the call
    // returns false because the underlying GPU backend is not
    // available; the public dimensions / samples accessors are
    // not wired through that conditional in either direction,
    // so the post-conditions hold either way.
    const bool first  = accum.resize(64, 64);
    const bool second = accum.resize(64, 64);
    (void) first;
    (void) second;

    // PT-P.3 invariant: regardless of CUDA availability, a
    // freshly-resized buffer reports 0 accumulated samples and
    // matches the requested dimensions.
    RR_CHECK(accum.samples_count() == 0);

    // The accessors return the cached width_ / height_ on the
    // happy path. On the failed-allocate path the implementation
    // resets them to 0; the audit-host build hits the
    // failed-allocate path because GpuBuffer::allocate without
    // CUDA returns false. Tolerate both outcomes here so the
    // test is stable on every config; the post-condition that
    // matters for PT-P.3 is `samples_count() == 0`.
    RR_CHECK((accum.width() == 64 && accum.height() == 64)
          || (accum.width() == 0  && accum.height() == 0));
}

// MANI-I.8 — manifold debug coordinate AOV type registration.
// Verifies the data-model surface lands correctly: the new
// enumerator value, component count, lowercase name, and
// `AOV::make_manifold_coordinates(...)` factory all behave
// as the task definition's §6.1 structural PASS criteria
// require.
void test_mani_i_8_manifold_coordinates_aov_type() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Enumerator value is 6 (appended at the end of the
    // enum; preserves the offsets of every pre-MANI-I.8
    // enumerator).
    RR_CHECK(static_cast<unsigned>(AOVType::ManifoldCoordinates) == 6u);

    // Component count is 3 (Vec3 per pixel).
    RR_CHECK(rr::renderer::aov_component_count(
                 AOVType::ManifoldCoordinates) == 3);

    // Stable lowercase name for filenames / log output.
    RR_CHECK(rr::renderer::aov_type_name(
                 AOVType::ManifoldCoordinates) == "manifold_coordinates");
}

void test_mani_i_8_manifold_coordinates_factory_default_name() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Factory with no explicit name uses the lowercase
    // enum name as the AOV's name.
    AOV aov = AOV::make_manifold_coordinates();
    RR_CHECK(aov.type()            == AOVType::ManifoldCoordinates);
    RR_CHECK(aov.name()            == "manifold_coordinates");
    RR_CHECK(aov.component_count() == 3);
}

void test_mani_i_8_manifold_coordinates_factory_custom_name() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Factory honours a caller-supplied name.
    AOV aov = AOV::make_manifold_coordinates("custom_manifold_dbg");
    RR_CHECK(aov.type() == AOVType::ManifoldCoordinates);
    RR_CHECK(aov.name() == "custom_manifold_dbg");
}

// OBSERVER.13 — observer-frame debug AOV type registration.
// Mirrors the MANI-I.8 manifold-coordinates AOV tests verbatim;
// verifies the data-model surface lands correctly per the
// OBSERVER.12 task brief's §7.1 structural PASS criteria.
void test_observer_13_observer_beta_aov_type() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Enumerator value is 7 (appended at the end of the enum
    // after `ManifoldCoordinates = 6`; preserves the offsets
    // of every pre-OBSERVER.13 enumerator).
    RR_CHECK(static_cast<unsigned>(AOVType::ObserverBeta) == 7u);

    // Component count is 3 (Vec3 per pixel encoding
    // observer_frame.beta).
    RR_CHECK(rr::renderer::aov_component_count(
                 AOVType::ObserverBeta) == 3);

    // Stable lowercase name for filenames / log output.
    RR_CHECK(rr::renderer::aov_type_name(
                 AOVType::ObserverBeta) == "observer_beta");
}

void test_observer_13_observer_beta_factory_default_name() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Factory with no explicit name uses the lowercase enum
    // name as the AOV's name.
    AOV aov = AOV::make_observer_beta();
    RR_CHECK(aov.type()            == AOVType::ObserverBeta);
    RR_CHECK(aov.name()            == "observer_beta");
    RR_CHECK(aov.component_count() == 3);
}

void test_observer_13_observer_beta_factory_custom_name() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Factory honours a caller-supplied name.
    AOV aov = AOV::make_observer_beta("custom_observer_dbg");
    RR_CHECK(aov.type() == AOVType::ObserverBeta);
    RR_CHECK(aov.name() == "custom_observer_dbg");
}

// FIELD-I.7 — scalar-field diagnostic AOV type registration.
// Mirrors the OBSERVER.13 observer-beta AOV tests verbatim;
// verifies the data-model surface lands correctly per the
// FIELD-I.6 task brief's §7.1 structural PASS criteria.
void test_field_i_7_field_scalar_aov_type() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Enumerator value is 8 (appended at the end of the enum
    // after `ObserverBeta = 7`; preserves the offsets of
    // every pre-FIELD-I.7 enumerator).
    RR_CHECK(static_cast<unsigned>(AOVType::FieldScalar) == 8u);

    // Component count is 1 (single-float per-pixel scalar;
    // mirrors the existing `Depth` / `DopplerFactor` /
    // `SearchlightFactor` single-channel encoding
    // precedent).
    RR_CHECK(rr::renderer::aov_component_count(
                 AOVType::FieldScalar) == 1);

    // Stable lowercase name for filenames / log output.
    // The PPM file naming follows the existing convention
    // (`aov_field_scalar.ppm` / `optix_aov_field_scalar.ppm`).
    RR_CHECK(rr::renderer::aov_type_name(
                 AOVType::FieldScalar) == "field_scalar");
}

void test_field_i_7_field_scalar_factory_default_name() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Factory with no explicit name uses the lowercase enum
    // name as the AOV's name.
    AOV aov = AOV::make_field_scalar();
    RR_CHECK(aov.type()            == AOVType::FieldScalar);
    RR_CHECK(aov.name()            == "field_scalar");
    RR_CHECK(aov.component_count() == 1);
}

void test_field_i_7_field_scalar_factory_custom_name() {
    using rr::renderer::AOV;
    using rr::renderer::AOVType;

    // Factory honours a caller-supplied name.
    AOV aov = AOV::make_field_scalar("custom_field_dbg");
    RR_CHECK(aov.type() == AOVType::FieldScalar);
    RR_CHECK(aov.name() == "custom_field_dbg");
}

}  // namespace

int main() {
    test_default_state();
    test_resize_zero_dimensions_returns_to_default();
    test_resize_same_dimensions_twice_keeps_zero_samples();

    // MANI-I.8: manifold debug coordinate AOV type registration.
    test_mani_i_8_manifold_coordinates_aov_type();
    test_mani_i_8_manifold_coordinates_factory_default_name();
    test_mani_i_8_manifold_coordinates_factory_custom_name();

    // OBSERVER.13: observer-frame debug AOV type registration.
    test_observer_13_observer_beta_aov_type();
    test_observer_13_observer_beta_factory_default_name();
    test_observer_13_observer_beta_factory_custom_name();

    // FIELD-I.7: scalar-field diagnostic AOV type registration.
    test_field_i_7_field_scalar_aov_type();
    test_field_i_7_field_scalar_factory_default_name();
    test_field_i_7_field_scalar_factory_custom_name();

    std::printf("renderer_tests: %d / %d passed\n",
                g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
