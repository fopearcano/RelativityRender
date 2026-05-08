#include "renderer/GpuAOVBuffer.h"

#include <utility>

namespace rr::renderer {

GpuAOVBuffer::GpuAOVBuffer(AOV aov)
    : aov_(std::move(aov)) {}

bool GpuAOVBuffer::resize(int width, int height) {
    // Drop any prior allocation first so a failed allocate leaves
    // the buffer empty rather than holding stale-size memory.
    reset();

    if (width == 0 && height == 0) {
        // Successful "clear" path, mirroring GpuMesh::upload_vertices
        // (host, 0).
        return true;
    }
    if (width <= 0 || height <= 0) {
        return false;
    }

    const int components = component_count();
    if (components <= 0) {
        // Unknown AOV type yields a zero / negative component count;
        // refuse to allocate rather than produce a buffer the
        // eventual sampler cannot interpret.
        return false;
    }

    const std::size_t float_count =
        static_cast<std::size_t>(width)
      * static_cast<std::size_t>(height)
      * static_cast<std::size_t>(components);

    if (!device_.allocate(float_count)) {
        // No CUDA backend, no visible device, or cudaMalloc failed.
        // Leave the buffer empty (reset() above already did this).
        return false;
    }

    width_  = width;
    height_ = height;
    return true;
}

void GpuAOVBuffer::reset() noexcept {
    device_.reset();
    width_  = 0;
    height_ = 0;
}

bool GpuAOVBuffer::download(std::vector<float>& host_dst) const {
    if (device_.empty()) {
        host_dst.clear();
        return false;
    }
    if (!valid()) {
        // Dimensions / buffer size are inconsistent. Refuse to
        // produce a download whose layout the caller can't trust.
        return false;
    }

    host_dst.resize(device_.size());
    if (!device_.download(host_dst.data(), device_.size())) {
        host_dst.clear();
        return false;
    }
    return true;
}

bool GpuAOVBuffer::valid() const noexcept {
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }
    const int components = component_count();
    if (components <= 0) {
        return false;
    }
    const std::size_t expected =
        static_cast<std::size_t>(width_)
      * static_cast<std::size_t>(height_)
      * static_cast<std::size_t>(components);
    return device_.size() == expected;
}

std::vector<GpuAOVBuffer> make_default_aov_set() {
    std::vector<GpuAOVBuffer> set;
    set.reserve(6);
    set.emplace_back(AOV::make_beauty());
    set.emplace_back(AOV::make_normal());
    set.emplace_back(AOV::make_depth());
    set.emplace_back(AOV::make_albedo());
    set.emplace_back(AOV::make_doppler_factor());
    set.emplace_back(AOV::make_searchlight_factor());
    return set;
}

}  // namespace rr::renderer
