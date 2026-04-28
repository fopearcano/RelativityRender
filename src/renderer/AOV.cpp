#include "renderer/AOV.h"

#include "image/Color.h"

namespace rr::renderer {

const char* aov_kind_name(AOVKind kind) {
    switch (kind) {
    case AOVKind::Beauty:            return "beauty";
    case AOVKind::Normal:            return "normal";
    case AOVKind::Depth:             return "depth";
    case AOVKind::Albedo:            return "albedo";
    case AOVKind::DopplerFactor:     return "dopplerFactor";
    case AOVKind::SearchlightFactor: return "searchlightFactor";
    }
    return "unknown";
}

bool aov_is_color(AOVKind kind) {
    return kind == AOVKind::Beauty
        || kind == AOVKind::Normal
        || kind == AOVKind::Albedo;
}

AOV::AOV(AOVKind kind, int width, int height)
    : kind_(kind),
      image_(width, height, rr::image::PixelFormat::Rgba32F) {}

bool AOV::save_ppm(const std::filesystem::path& path) const {
    if (image_.empty()) {
        return false;
    }

    // Colour AOVs save through the standard image path. `Image::save_ppm`
    // already clamps to [0, 1] and drops alpha, which is exactly what the
    // v1 viewer hook needs.
    if (aov_is_color(kind_)) {
        return image_.save_ppm(path);
    }

    // Scalar AOV: the value lives in the R channel. Find the brightest
    // pixel, normalise so it maps to 1.0, and emit a grayscale triple.
    // This keeps depth / Doppler / searchlight images human-readable
    // without committing the renderer to a tone-mapping policy yet.
    const int w = image_.width();
    const int h = image_.height();

    float max_val = 0.0f;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto px = image_.get_pixel(x, y);
            if (px.r > max_val) {
                max_val = px.r;
            }
        }
    }
    if (max_val <= 0.0f) {
        max_val = 1.0f;
    }
    const float inv = 1.0f / max_val;

    rr::image::Image gray(w, h, rr::image::PixelFormat::Rgba32F);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto px = image_.get_pixel(x, y);
            const float v = px.r * inv;
            gray.set_pixel(x, y, rr::image::Rgba(v, v, v, 1.0f));
        }
    }
    return gray.save_ppm(path);
}

}
