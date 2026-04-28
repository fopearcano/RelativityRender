#include "optix/OptixRenderer.h"

#include "optix/OptixBackend.h"

namespace rr::optix {

OptixRenderer::Result OptixRenderer::render_placeholder(int width, int height) {
    Result r;

    if (width <= 0 || height <= 0) {
        r.message = "OptiX render: invalid dimensions";
        return r;
    }

    if (!optix_backend_available()) {
        r.message = "OptiX render: backend not compiled in "
                    "(rebuild with RELATIVITYRENDER_ENABLE_OPTIX=ON)";
        return r;
    }

    // Probe the runtime so the message reports more than just
    // "compiled in".
    OptixBackend probe;
    if (!probe.init()) {
        r.message = "OptiX render: runtime init failed: " + probe.last_error();
        return r;
    }

    r.message = "OptiX render: scaffold only - rendering arrives in M15.4 "
                "(see docs/OPTIX_BACKEND_PLAN.md). Runtime initialisation OK.";
    return r;
}

}
