// relativity-core-v1 day-1 entry point.
//
// Two responsibilities only:
//   1. Report which GPU backend is compiled in and enumerate visible
//      devices (CUDA detection).
//   2. Run a single GPU diagnostic - the UV-gradient kernel - and save
//      the resulting image as PPM ("GPU gradient render working").
//
// No scene loading, no path tracer, no relativity, no server.
// Everything beyond CUDA detection + gradient render is deferred to
// dedicated rewrite slices listed in `docs/REWRITE_STATUS.md`.
//
// Usage:
//   RelativityRender                                  # detect + render 256x256
//   RelativityRender --detect                         # detect only
//   RelativityRender --render-gradient WxH OUTPUT     # render only

#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"
#include "image/Image.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaRenderer.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

struct Args {
    bool        detect_only = false;
    bool        render_only = false;
    int         width       = 256;
    int         height      = 256;
    std::string output_path = "gradient.ppm";
};

void print_usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s                                  # detect + render 256x256\n"
        "  %s --detect                         # detect only\n"
        "  %s --render-gradient WxH OUTPUT     # render only\n",
        argv0, argv0, argv0);
}

bool parse_size(std::string_view s, int& w, int& h) {
    const auto x = s.find('x');
    if (x == std::string_view::npos) return false;
    try {
        w = std::stoi(std::string(s.substr(0, x)));
        h = std::stoi(std::string(s.substr(x + 1)));
    } catch (...) {
        return false;
    }
    return w > 0 && h > 0;
}

bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--detect") {
            out.detect_only = true;
        } else if (a == "--render-gradient") {
            out.render_only = true;
            if (i + 2 >= argc) return false;
            if (!parse_size(argv[++i], out.width, out.height)) return false;
            out.output_path = argv[++i];
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            std::fprintf(stderr, "unknown arg: %.*s\n",
                         static_cast<int>(a.size()), a.data());
            return false;
        }
    }
    return true;
}

void report_gpu_detection() {
    rr::core::Logger::info(std::string("GPU backend: ") + rr::gpu::gpu_backend_name());
    const auto devices = rr::gpu::enumerate_devices();
    if (devices.empty()) {
        rr::core::Logger::info("No CUDA-capable devices visible. "
                               "Rebuild with -DRR_ENABLE_CUDA=ON on a GPU host "
                               "to enable rendering.");
        return;
    }
    rr::core::Logger::info("Visible devices:");
    for (const auto& d : devices) {
        std::string line = "  [" + std::to_string(d.index) + "] " + d.name
                         + " (sm_" + d.compute_capability_string()
                         + ", " + d.total_memory_human()
                         + ", " + std::to_string(d.multiprocessor_count) + " SMs)";
        rr::core::Logger::info(line);
    }
}

bool render_gradient(const Args& args) {
#ifdef RR_HAS_CUDA
    auto r = rr::cuda::CudaRenderer::render_gradient(args.width, args.height);
    if (!r.ok) {
        rr::core::Logger::error(std::string("Gradient render failed: ") + r.message);
        return false;
    }
    if (!r.image.save_ppm(args.output_path)) {
        rr::core::Logger::error("Failed to write PPM: " + args.output_path);
        return false;
    }
    rr::core::Logger::info("Wrote gradient: " + args.output_path
                           + " (" + std::to_string(args.width) + "x"
                           + std::to_string(args.height) + ")");
    return true;
#else
    (void)args;
    rr::core::Logger::error(
        "CUDA backend not compiled in. Rebuild with -DRR_ENABLE_CUDA=ON "
        "on a host with the CUDA Toolkit and a CUDA-capable GPU.");
    return false;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        print_usage(argv[0]);
        return 2;
    }

    rr::core::Logger::info(std::string("RelativityRender ")
                           + rr::core::kVersionString + " (relativity-core-v1)");

    if (!args.render_only) {
        report_gpu_detection();
    }

    if (args.detect_only) {
        return 0;
    }

    return render_gradient(args) ? 0 : 1;
}
