// RelativityRender entry point.
//
// Stage 6 scope: parse command-line flags and dispatch a
// stage-appropriate response. `--device-info` enumerates CUDA devices
// (Module 6 of the master order); `--render-gradient` launches the
// GPU UV-gradient diagnostic kernel (Module 7) and saves a PPM. No
// scene system, no path tracer, no server, no C4D.

#include "core/CommandLine.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "gpu/GpuDevice.h"

#ifdef RR_HAS_CUDA
    #include "cuda/CudaRenderer.h"
#endif

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

void report_device_info() {
    using rr::core::Logger;

    Logger::info(std::string("GPU backend: ") + rr::gpu::gpu_backend_name());

    const auto devices = rr::gpu::enumerate_devices();
    if (devices.empty()) {
        Logger::info("No CUDA-capable devices visible. "
                     "Rebuild with -DRR_ENABLE_CUDA=ON on a host with the "
                     "CUDA Toolkit and a CUDA-capable GPU to enable device "
                     "queries.");
        return;
    }

    Logger::info(std::to_string(devices.size())
                 + (devices.size() == 1 ? " device:" : " devices:"));
    for (const auto& d : devices) {
        const std::string line =
            "  [" + std::to_string(d.index) + "] " + d.name
          + " (sm_"  + d.compute_capability_string()
          + ", "     + d.total_memory_human()
          + ", "     + std::to_string(d.multiprocessor_count) + " SMs)";
        Logger::info(line);
    }
}

// `--render-gradient` dispatch. Width / height come from Config (the
// CLI's --width / --height knobs); output path defaults to
// "output/gpu_gradient.ppm" but is overridden by --output.
//
// Returns the process exit code: 0 on success, 1 on any failure.
int run_render_gradient(const rr::core::Config& cfg) {
    using rr::core::Logger;

    const std::string out_path = cfg.output_path.empty()
        ? std::string("output/gpu_gradient.ppm")
        : cfg.output_path;

#ifndef RR_HAS_CUDA
    (void)cfg;
    Logger::error("--render-gradient requires CUDA. Rebuild with "
                  "-DRR_ENABLE_CUDA=ON on a host with the CUDA Toolkit "
                  "and a CUDA-capable GPU.");
    return 1;
#else
    auto r = rr::cuda::CudaRenderer::render_gradient(cfg.width, cfg.height);
    if (!r.ok) {
        Logger::error("gradient render failed: " + r.message);
        return 1;
    }

    // Make sure the output directory exists before writing. Image's
    // save_ppm does not create parent directories.
    namespace fs = std::filesystem;
    const fs::path out_fs = out_path;
    if (out_fs.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_fs.parent_path(), ec);
        if (ec) {
            Logger::error("could not create output directory '"
                        + out_fs.parent_path().string() + "': "
                        + ec.message());
            return 1;
        }
    }

    if (!r.image.save_ppm(out_fs)) {
        Logger::error("could not write PPM: " + out_path);
        return 1;
    }

    std::error_code ec;
    const fs::path  abs = fs::absolute(out_fs, ec);
    Logger::info("wrote GPU gradient: "
               + (ec ? out_path : abs.string())
               + " (" + std::to_string(cfg.width) + "x"
               + std::to_string(cfg.height) + ", RGBA32F)");
    return 0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    using rr::core::CommandLine;
    using rr::core::Logger;

    const auto result = CommandLine::parse(argc, argv);

    switch (result.action) {
        case CommandLine::Action::Help:
            std::cout << CommandLine::usage(argv[0]);
            return 0;

        case CommandLine::Action::Version:
            std::cout << CommandLine::version_string() << '\n';
            return 0;

        case CommandLine::Action::DeviceInfo:
            report_device_info();
            return 0;

        case CommandLine::Action::Render:
            Logger::info("render command received");
            return 0;

        case CommandLine::Action::RenderGradient:
            return run_render_gradient(result.config);

        case CommandLine::Action::Error:
            Logger::error(result.error_message);
            std::cerr << CommandLine::usage(argv[0]);
            return 2;

        case CommandLine::Action::Default:
            Logger::info(std::string(rr::core::kProjectName) + " "
                       + rr::core::kVersionString + " starting up.");
            Logger::info("Stage 6: CUDA kernel infrastructure. "
                         "Try --device-info or --render-gradient.");
            return 0;
    }
    return 0;
}
