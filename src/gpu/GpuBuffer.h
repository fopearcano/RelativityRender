#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace rr::gpu {

namespace detail {

// Byte-level backend primitives. Implemented in `GpuBuffer.cpp`, which
// forwards to the CUDA backend (`rr::cuda::`) when `RR_HAS_CUDA` is
// defined. With no backend compiled in, allocation returns nullptr and
// copies return false; `GpuBuffer<T>` surfaces these as honest failure
// states without crashing the host build.
[[nodiscard]] void* gpu_alloc(std::size_t bytes);
void                gpu_free (void* device_ptr) noexcept;
[[nodiscard]] bool  gpu_copy_host_to_device(void* device_dst, const void* host_src,   std::size_t bytes);
[[nodiscard]] bool  gpu_copy_device_to_host(void* host_dst,   const void* device_src, std::size_t bytes);

}

// Typed, move-only owning handle for a device-side buffer of `T`.
//
// Allocation is deferred: constructing a `GpuBuffer` does not touch the
// GPU. All copy operations are synchronous; async streaming arrives in
// a later milestone with `cuda::Stream`.
//
// `T` must be trivially copyable - the backend just shuffles bytes.
template <typename T>
class GpuBuffer {
    static_assert(std::is_trivially_copyable_v<T>,
                  "GpuBuffer<T> requires a trivially copyable element type");

public:
    GpuBuffer() = default;
    ~GpuBuffer() { reset(); }

    GpuBuffer(const GpuBuffer&)            = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    GpuBuffer(GpuBuffer&& other) noexcept
        : ptr_(other.ptr_), count_(other.count_) {
        other.ptr_   = nullptr;
        other.count_ = 0;
    }

    GpuBuffer& operator=(GpuBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_         = other.ptr_;
            count_       = other.count_;
            other.ptr_   = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    // Reserve `count` elements of device memory. Replaces any prior
    // allocation. Returns false if no GPU backend is compiled in or
    // the device-side allocation failed; on failure the buffer is
    // empty.
    [[nodiscard]] bool allocate(std::size_t count) {
        reset();
        if (count == 0) return true;
        void* p = detail::gpu_alloc(count * sizeof(T));
        if (!p) return false;
        ptr_   = p;
        count_ = count;
        return true;
    }

    // Resize (if needed) and copy `count` elements from host memory
    // into the device buffer.
    [[nodiscard]] bool upload(const T* host_src, std::size_t count) {
        if (count_ != count) {
            if (!allocate(count)) return false;
        }
        if (count == 0) return true;
        return detail::gpu_copy_host_to_device(ptr_, host_src, count * sizeof(T));
    }

    // Copy `count` elements from the device buffer into host memory.
    // Returns false if `count` exceeds the current buffer size or the
    // backend copy failed.
    [[nodiscard]] bool download(T* host_dst, std::size_t count) const {
        if (count > count_) return false;
        if (count == 0) return true;
        return detail::gpu_copy_device_to_host(host_dst, ptr_, count * sizeof(T));
    }

    // Free the device allocation. Safe to call repeatedly and on a
    // moved-from / never-allocated buffer.
    void reset() noexcept {
        if (ptr_) {
            detail::gpu_free(ptr_);
            ptr_   = nullptr;
            count_ = 0;
        }
    }

    [[nodiscard]] bool        empty()         const noexcept { return count_ == 0; }
    [[nodiscard]] std::size_t size()          const noexcept { return count_; }
    [[nodiscard]] std::size_t size_in_bytes() const noexcept { return count_ * sizeof(T); }
    [[nodiscard]] T*          device_ptr()          noexcept { return static_cast<T*>(ptr_); }
    [[nodiscard]] const T*    device_ptr()    const noexcept { return static_cast<const T*>(ptr_); }

private:
    void*       ptr_   = nullptr;
    std::size_t count_ = 0;
};

}
