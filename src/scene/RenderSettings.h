#pragma once

namespace rr::scene {

// Per-render settings that are not part of the camera or the
// relativity model. Resolution lives here rather than on the camera
// because it depends on the output surface, not the optical
// configuration.
//
// `samples_per_pixel` and `max_depth` are stored faithfully but not
// consumed yet - the path tracer (master module 16) is the first
// renderer that will read them. Keeping them on `RenderSettings`
// from Stage 6A means the scene-format / loader work in module 15
// can populate them without revisiting this header.
struct RenderSettings {
    int width             = 1280;
    int height            = 720;
    int samples_per_pixel = 1;
    int max_depth         = 1;     // 1 = primary rays only (current state).
};

}
